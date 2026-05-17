#include "LoxoneOneWireManager.hpp"
#include "dallas_crc.hpp"
#include "eventStructs.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include <serialization.hpp>
#include <span>
#include <algorithm>

LoxoneOneWireManager::LoxoneOneWireManager(const espConfig::loxone_config_t& config)
    : m_config(config), m_gpio(static_cast<gpio_num_t>(config.gpioPin)) {}

LoxoneOneWireManager::~LoxoneOneWireManager() {
    if (m_taskHandle) vTaskDelete(m_taskHandle);
    if (m_resetSem)   vSemaphoreDelete(m_resetSem);
    gpio_isr_handler_remove(m_gpio);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void LoxoneOneWireManager::begin() {
    if (!m_config.enabled) return;

    // Configure GPIO4 as open-drain: drive LOW to pull bus down, write 1 to float
    gpio_config_t io = {
        .pin_bit_mask  = 1ULL << m_gpio,
        .mode          = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en    = GPIO_PULLUP_DISABLE,   // external 4.7kΩ pull-up on board
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_ANYEDGE,     // ISR on both rising and falling
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(m_gpio, 1);  // release bus (float to pull-up)

    m_resetSem = xSemaphoreCreateBinary();
    configASSERT(m_resetSem);

    // gpio_install_isr_service is idempotent when called multiple times
    gpio_install_isr_service(0);
    gpio_isr_handler_add(m_gpio, isrHandler, this);

    // High-priority task on core 1 to avoid interference with NFC on core 0
    BaseType_t ok = xTaskCreatePinnedToCore(
        taskEntry, "ow_slave", 4096, this, 20, &m_taskHandle, 1
    );
    configASSERT(ok == pdPASS);

    // Subscribe to NFC_TAP_EVENT on the EventBus
    m_nfcEventSub = subscribe(NFC_EVENT, NFC_TAP_EVENT,
        [this](const uint8_t* data, size_t size) {
            if (!data || size == 0) return;
            std::span<const uint8_t> payload(data, size);
            std::error_code ec;
            NfcEvent nfc_event = alpaca::deserialize<NfcEvent>(payload, ec);
            if (ec || nfc_event.type != HOMEKEY_TAP) return;

            EventHKTap tap = alpaca::deserialize<EventHKTap>(nfc_event.data, ec);
            if (ec || !tap.status) return;

            // Convert issuerId bytes to lowercase hex string
            std::string issuerHex;
            issuerHex.reserve(tap.issuerId.size() * 2);
            for (uint8_t b : tap.issuerId) {
                char buf[3];
                snprintf(buf, sizeof(buf), "%02x", b);
                issuerHex += buf;
            }
            ESP_LOGI(TAG, "HomeKey tap — issuerId: %s", issuerHex.c_str());

            for (const auto& mapping : m_config.mappings) {
                if (mapping.issuerId == issuerHex) {
                    ESP_LOGI(TAG, "Mapped to ROM for '%s' — activating 1-Wire", mapping.label.c_str());
                    activateRom(mapping.romCode);
                    return;
                }
            }
            ESP_LOGW(TAG, "No 1-Wire mapping for issuerId %s — add via POST /loxone/mappings", issuerHex.c_str());
        });

    ESP_LOGI(TAG, "1-Wire slave started on GPIO%d", m_gpio);
}

void LoxoneOneWireManager::activateRom(const espConfig::ibutton_rom_t& rom) {
    m_activeRom   = rom;
    m_activeUntil = esp_timer_get_time() + (int64_t)m_config.activeDurationMs * 1000;
    m_active.store(true, std::memory_order_release);
    ESP_LOGI(TAG, "ROM active for %u ms: %02X %02X %02X %02X %02X %02X %02X %02X",
        m_config.activeDurationMs,
        rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
}

// ---------------------------------------------------------------------------
// ISR — runs in interrupt context, no heap, no logging
// ---------------------------------------------------------------------------

void IRAM_ATTR LoxoneOneWireManager::isrHandler(void* arg) {
    static_cast<LoxoneOneWireManager*>(arg)->handleEdge();
}

void IRAM_ATTR LoxoneOneWireManager::handleEdge() {
    int     level = gpio_get_level(m_gpio);
    int64_t now   = esp_timer_get_time();

    if (level == 0) {
        // Falling edge: record timestamp for duration measurement
        m_tFall = now;
    } else {
        // Rising edge: compute LOW duration
        int64_t dur = now - m_tFall;
        if (dur >= RESET_PULSE_MIN_US) {
            BaseType_t woken = pdFALSE;
            xSemaphoreGiveFromISR(m_resetSem, &woken);
            portYIELD_FROM_ISR(woken);
        }
    }
}

// ---------------------------------------------------------------------------
// Task entry
// ---------------------------------------------------------------------------

void LoxoneOneWireManager::taskEntry(void* arg) {
    static_cast<LoxoneOneWireManager*>(arg)->owTask();
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// 1-Wire slave state machine (runs in owTask, timing-critical sections
// use portDISABLE_INTERRUPTS to prevent FreeRTOS preemption)
// ---------------------------------------------------------------------------

void LoxoneOneWireManager::owTask() {
    ESP_LOGI(TAG, "1-Wire slave task running on core %d", xPortGetCoreID());

    while (true) {
        if (xSemaphoreTake(m_resetSem, portMAX_DELAY) != pdTRUE) continue;

        // Check active window
        if (!m_active.load(std::memory_order_acquire)) continue;
        if (esp_timer_get_time() > m_activeUntil) {
            m_active.store(false, std::memory_order_release);
            ESP_LOGI(TAG, "1-Wire active window expired");
            continue;
        }

        portDISABLE_INTERRUPTS();

        sendPresence();
        uint8_t cmd = receiveByte();

        ESP_EARLY_LOGD(TAG, "OW command: 0x%02X", cmd);

        switch (cmd) {
            case CMD_READ_ROM:
                handleReadRom();
                break;
            case CMD_SEARCH_ROM:
                handleSearchRom();
                break;
            case CMD_SKIP_ROM:
                // DS1990A has no function commands — nothing follows Skip ROM
                ESP_EARLY_LOGD(TAG, "Skip ROM received");
                break;
            default:
                ESP_EARLY_LOGW(TAG, "Unknown OW command: 0x%02X", cmd);
                break;
        }

        portENABLE_INTERRUPTS();
    }
}

// ---------------------------------------------------------------------------
// 1-Wire protocol primitives
// ---------------------------------------------------------------------------

void LoxoneOneWireManager::sendPresence() {
    // Master released bus after reset — wait, then pull LOW for presence
    ets_delay_us(PRESENCE_DELAY_US);
    gpio_set_level(m_gpio, 0);          // drive LOW (presence pulse)
    ets_delay_us(PRESENCE_PULSE_US);
    gpio_set_level(m_gpio, 1);          // release (pull-up restores HIGH)
    ets_delay_us(10);
}

bool LoxoneOneWireManager::receiveBit() {
    // Wait for master to initiate bit slot (falling edge)
    uint32_t timeout = 10000;
    while (gpio_get_level(m_gpio) == 1 && --timeout) {
        ets_delay_us(1);
    }
    if (!timeout) return false;

    // Sample 30µs into the bit slot
    ets_delay_us(BIT_SAMPLE_US);
    bool bit = gpio_get_level(m_gpio) == 1;

    // Wait for end of slot (70µs total from falling edge)
    ets_delay_us(BIT_SLOT_US - BIT_SAMPLE_US);
    return bit;
}

uint8_t LoxoneOneWireManager::receiveByte() {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        if (receiveBit()) byte |= (1 << i);  // LSB first (1-Wire)
    }
    return byte;
}

void LoxoneOneWireManager::sendBit(bool bit) {
    // Wait for master to initiate bit slot
    uint32_t timeout = 10000;
    while (gpio_get_level(m_gpio) == 1 && --timeout) {
        ets_delay_us(1);
    }
    if (!timeout) return;

    if (!bit) {
        // Send 0: extend the LOW beyond master's release point
        ets_delay_us(5);
        gpio_set_level(m_gpio, 0);  // actively pull LOW
        ets_delay_us(55);           // hold through master's sample point (~30µs)
        gpio_set_level(m_gpio, 1);  // release
        ets_delay_us(10);
    } else {
        // Send 1: don't drive — pull-up holds HIGH after master releases
        ets_delay_us(BIT_SLOT_US);
    }
}

void LoxoneOneWireManager::sendByte(uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        sendBit((byte >> i) & 1);  // LSB first
    }
}

// ---------------------------------------------------------------------------
// ROM command handlers
// ---------------------------------------------------------------------------

void LoxoneOneWireManager::handleReadRom() {
    ESP_EARLY_LOGI(TAG, "Read ROM: %02X %02X %02X %02X %02X %02X %02X %02X",
        m_activeRom[0], m_activeRom[1], m_activeRom[2], m_activeRom[3],
        m_activeRom[4], m_activeRom[5], m_activeRom[6], m_activeRom[7]);
    for (int i = 0; i < 8; i++) {
        sendByte(m_activeRom[i]);
    }
}

void LoxoneOneWireManager::handleSearchRom() {
    // Bit-by-bit search ROM for single device on bus
    // Master reads bit, reads complement, writes choice; we always match
    for (int bit_idx = 0; bit_idx < 64; bit_idx++) {
        bool rom_bit  = (m_activeRom[bit_idx / 8] >> (bit_idx % 8)) & 1;
        bool rom_comp = !rom_bit;

        sendBit(rom_bit);
        sendBit(rom_comp);

        bool master_choice = receiveBit();

        // If master chose a different branch, go silent — we're not on that path
        if (master_choice != rom_bit) {
            ESP_EARLY_LOGD(TAG, "Search ROM: diverged at bit %d", bit_idx);
            return;
        }
    }
    ESP_EARLY_LOGI(TAG, "Search ROM: device fully selected");
}
