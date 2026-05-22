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
    if (m_deactivateTimer) {
        esp_timer_stop(m_deactivateTimer);
        esp_timer_delete(m_deactivateTimer);
    }
    if (m_taskHandle) vTaskDelete(m_taskHandle);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void LoxoneOneWireManager::begin() {
    if (!m_config.enabled) return;

    gpio_config_t io = {
        .pin_bit_mask  = 1ULL << m_gpio,
        .mode          = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en    = GPIO_PULLUP_DISABLE,   // external 4.7kΩ pull-up
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,     // polling — no ISR needed
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(m_gpio, 1);  // release bus

    const esp_timer_create_args_t timerArgs = {
        .callback        = deactivateCallback,
        .arg             = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "ow_deactivate",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timerArgs, &m_deactivateTimer));

    // High-priority task on core 1 — polls GPIO directly for timing accuracy
    BaseType_t ok = xTaskCreatePinnedToCore(
        taskEntry, "ow_slave", 4096, this, 20, &m_taskHandle, 1
    );
    configASSERT(ok == pdPASS);

    m_nfcEventSub = AppEventLoop::subscribe(NFC_EVENT, NFC_TAP_EVENT,
        [this](const uint8_t* data, size_t size) {
            if (!data || size == 0) return;
            std::span<const uint8_t> payload(data, size);
            std::error_code ec;
            NfcEvent nfc_event = alpaca::deserialize<NfcEvent>(payload, ec);
            if (ec || nfc_event.type != HOMEKEY_TAP) return;

            EventHKTap tap = alpaca::deserialize<EventHKTap>(nfc_event.data, ec);
            if (ec || !tap.status) return;

            const auto& sourceBytes = (m_config.romSource == 1) ? tap.endpointId : tap.issuerId;
            const char* sourceName  = (m_config.romSource == 1) ? "endpointId" : "issuerId";

            std::string sourceHex;
            sourceHex.reserve(sourceBytes.size() * 2);
            for (uint8_t b : sourceBytes) {
                char buf[3];
                snprintf(buf, sizeof(buf), "%02x", b);
                sourceHex += buf;
            }

            espConfig::ibutton_rom_t rom{};
            rom[0] = 0x01;
            for (size_t i = 0; i < 6 && i < sourceBytes.size(); i++) {
                rom[i + 1] = sourceBytes[i];
            }
            dallas_rom_set_crc(rom);

            ESP_LOGI(TAG, "%s: %s → ROM: %02X%02X%02X%02X%02X%02X%02X%02X",
                sourceName, sourceHex.c_str(),
                rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
            ESP_LOGI(TAG, "Add to Loxone (if new): %02X%02X%02X%02X%02X%02X%02X%02X",
                rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);

            activateRom(rom);
        });

    ESP_LOGI(TAG, "1-Wire slave started on GPIO%d (polling mode)", m_gpio);
}

void LoxoneOneWireManager::activateRom(const espConfig::ibutton_rom_t& rom) {
    m_activeRom = rom;
    esp_timer_stop(m_deactivateTimer);
    esp_timer_start_once(m_deactivateTimer, (uint64_t)m_config.activeDurationMs * 1000ULL);
    m_active.store(true, std::memory_order_release);
    ESP_LOGI(TAG, "ROM active for %u ms: %02X %02X %02X %02X %02X %02X %02X %02X",
        m_config.activeDurationMs,
        rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
}

void LoxoneOneWireManager::deactivateCallback(void* arg) {
    auto* self = static_cast<LoxoneOneWireManager*>(arg);
    self->m_active.store(false, std::memory_order_release);
    ESP_LOGI(TAG, "1-Wire active window expired — device now absent on bus");
}

// ---------------------------------------------------------------------------
// Task entry
// ---------------------------------------------------------------------------

void LoxoneOneWireManager::taskEntry(void* arg) {
    static_cast<LoxoneOneWireManager*>(arg)->owTask();
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// 1-Wire slave state machine
//
// Uses direct GPIO polling instead of ISR+semaphore. Reason: the ISR fires on
// the core where gpio_install_isr_service was called (core 0), but owTask runs
// on core 1. FreeRTOS inter-core wakeup latency is 50-200µs — far beyond the
// 15-60µs presence-detect window. Polling inside the task eliminates this
// latency entirely: the presence pulse is sent from the same execution context
// that detected the rising edge of the reset pulse.
// ---------------------------------------------------------------------------

void LoxoneOneWireManager::owTask() {
    ESP_LOGI(TAG, "1-Wire slave task running on core %d", xPortGetCoreID());

    while (true) {
        if (!m_active.load(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(5));  // sleep when no device to emulate
            continue;
        }

        // Poll for reset pulse and send presence immediately on detection
        if (!detectResetAndPresence()) continue;

        // Disable scheduler preemption during timing-critical bit exchange
        portDISABLE_INTERRUPTS();

        uint8_t cmd = receiveByte();
        ESP_EARLY_LOGI(TAG, "OW cmd: 0x%02X", cmd);

        switch (cmd) {
            case CMD_READ_ROM:
                handleReadRom();
                break;
            case CMD_SEARCH_ROM:
                handleSearchRom();
                break;
            case CMD_MATCH_ROM:
                handleMatchRom();
                break;
            case CMD_SKIP_ROM:
                ESP_EARLY_LOGD(TAG, "Skip ROM");
                break;
            default:
                ESP_EARLY_LOGW(TAG, "Unknown OW cmd: 0x%02X", cmd);
                break;
        }

        portENABLE_INTERRUPTS();
    }
}

// ---------------------------------------------------------------------------
// Reset detection + presence pulse (polling, no ISR)
// ---------------------------------------------------------------------------

bool LoxoneOneWireManager::detectResetAndPresence() {
    // Drain any ongoing LOW on the bus before looking for a new reset
    {
        uint32_t drain = 100000;
        while (gpio_get_level(m_gpio) == 0 && --drain) {
            ets_delay_us(1);
        }
        if (!drain) return false;
    }

    // Wait for falling edge (start of reset pulse). 1µs poll keeps timing
    // accurate without burning 100 % CPU when the bus is idle for a long time.
    while (gpio_get_level(m_gpio) == 1) {
        if (!m_active.load(std::memory_order_acquire)) return false;
        ets_delay_us(1);
    }
    int64_t t_fall = esp_timer_get_time();

    // Wait for rising edge (reset pulse ends)
    while (gpio_get_level(m_gpio) == 0) {
        if (esp_timer_get_time() - t_fall > 10000) return false;  // 10ms sanity
    }
    int64_t dur = esp_timer_get_time() - t_fall;

    if (dur < RESET_PULSE_MIN_US) return false;  // spurious glitch
    if (!m_active.load(std::memory_order_acquire)) return false;

    // Valid reset — send presence pulse immediately (within window 15-60µs)
    ets_delay_us(PRESENCE_DELAY_US);   // 30µs post-reset delay
    gpio_set_level(m_gpio, 0);          // pull bus LOW (presence)
    ets_delay_us(PRESENCE_PULSE_US);    // 120µs
    gpio_set_level(m_gpio, 1);          // release
    ets_delay_us(10);

    return true;
}

// ---------------------------------------------------------------------------
// 1-Wire protocol primitives
// ---------------------------------------------------------------------------

bool LoxoneOneWireManager::receiveBit() {
    // Wait for master's falling edge (bit slot start)
    uint32_t timeout = 10000;
    while (gpio_get_level(m_gpio) == 1 && --timeout) {
        ets_delay_us(1);
    }
    if (!timeout) return false;

    // Sample 30µs into the slot
    ets_delay_us(BIT_SAMPLE_US);
    bool bit = gpio_get_level(m_gpio) == 1;

    // Wait for remaining slot time
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
    // Wait for master's falling edge (read slot start)
    uint32_t timeout = 10000;
    while (gpio_get_level(m_gpio) == 1 && --timeout) {
        ets_delay_us(1);
    }
    if (!timeout) return;

    if (!bit) {
        // Send 0: extend LOW past master's 30µs sample point
        ets_delay_us(5);
        gpio_set_level(m_gpio, 0);
        ets_delay_us(55);
        gpio_set_level(m_gpio, 1);
        ets_delay_us(10);
    } else {
        // Send 1: release — pull-up holds HIGH through sample point
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
    for (int bit_idx = 0; bit_idx < 64; bit_idx++) {
        bool rom_bit  = (m_activeRom[bit_idx / 8] >> (bit_idx % 8)) & 1;
        bool rom_comp = !rom_bit;

        sendBit(rom_bit);
        sendBit(rom_comp);

        bool master_choice = receiveBit();
        if (master_choice != rom_bit) {
            ESP_EARLY_LOGD(TAG, "Search ROM: diverged at bit %d", bit_idx);
            return;
        }
    }
    ESP_EARLY_LOGI(TAG, "Search ROM: device fully selected");
}

void LoxoneOneWireManager::handleMatchRom() {
    // Receive 8-byte ROM from master and check if it's us
    uint8_t received[8];
    for (int i = 0; i < 8; i++) {
        received[i] = receiveByte();
    }
    bool match = true;
    for (int i = 0; i < 8; i++) {
        if (received[i] != m_activeRom[i]) { match = false; break; }
    }
    ESP_EARLY_LOGI(TAG, "Match ROM: %s", match ? "selected" : "not us");
    // DS1990A has no function commands after Match ROM — nothing more to do
}
