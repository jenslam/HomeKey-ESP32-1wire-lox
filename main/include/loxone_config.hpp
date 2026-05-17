#pragma once
#include <array>
#include <cstdint>
#ifndef CONFIG_LOXONE_ONEWIRE_GPIO
#define CONFIG_LOXONE_ONEWIRE_GPIO 4
#endif
#ifndef CONFIG_LOXONE_ACTIVE_DURATION_MS
#define CONFIG_LOXONE_ACTIVE_DURATION_MS 3000
#endif

namespace espConfig {

// 8-byte DS1990A ROM: [0x01][6-byte serial][CRC8]
using ibutton_rom_t = std::array<uint8_t, 8>;

struct loxone_config_t {
    bool     enabled          = true;
    uint8_t  gpioPin          = CONFIG_LOXONE_ONEWIRE_GPIO;
    uint16_t activeDurationMs = CONFIG_LOXONE_ACTIVE_DURATION_MS;
};

} // namespace espConfig
