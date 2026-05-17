#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "loxone_config.hpp"
#include "app_event_loop.hpp"

class LoxoneOneWireManager {
public:
    explicit LoxoneOneWireManager(const espConfig::loxone_config_t& config);
    ~LoxoneOneWireManager();

    void begin();
    void activateRom(const espConfig::ibutton_rom_t& rom);

    // Public for C-style ISR callback
    void IRAM_ATTR handleEdge();
    void owTask();

private:
    static constexpr const char* TAG = "LoxoneOneWire";

    // 1-Wire timing constants (µs)
    static constexpr uint32_t RESET_PULSE_MIN_US = 480;
    static constexpr uint32_t PRESENCE_DELAY_US  = 30;
    static constexpr uint32_t PRESENCE_PULSE_US  = 120;
    static constexpr uint32_t BIT_SAMPLE_US      = 30;
    static constexpr uint32_t BIT_SLOT_US        = 70;

    // DS1990A ROM command bytes
    static constexpr uint8_t CMD_READ_ROM   = 0x33;
    static constexpr uint8_t CMD_SEARCH_ROM = 0xF0;
    static constexpr uint8_t CMD_SKIP_ROM   = 0xCC;

    const espConfig::loxone_config_t& m_config;
    SubscriptionHandle                 m_nfcEventSub;

    gpio_num_t        m_gpio;
    SemaphoreHandle_t m_resetSem    = nullptr;
    TaskHandle_t      m_taskHandle  = nullptr;

    volatile int64_t  m_tFall = 0;  // timestamp of last falling edge (µs)

    std::atomic<bool>        m_active{false};
    int64_t                  m_activeUntil = 0;
    espConfig::ibutton_rom_t m_activeRom{};

    void     sendPresence();
    uint8_t  receiveByte();
    void     sendByte(uint8_t byte);
    void     sendBit(bool bit);
    bool     receiveBit();
    void     handleReadRom();
    void     handleSearchRom();

    static void IRAM_ATTR isrHandler(void* arg);
    static void           taskEntry(void* arg);
};
