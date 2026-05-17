#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#ifndef CONFIG_LOXONE_ONEWIRE_GPIO
#define CONFIG_LOXONE_ONEWIRE_GPIO 4
#endif
#ifndef CONFIG_LOXONE_ACTIVE_DURATION_MS
#define CONFIG_LOXONE_ACTIVE_DURATION_MS 3000
#endif
#ifndef CONFIG_LOXONE_MAX_MAPPINGS
#define CONFIG_LOXONE_MAX_MAPPINGS 16
#endif

namespace espConfig {

// 8-byte DS1990A ROM: [0x01][6-byte serial][CRC8]
using ibutton_rom_t = std::array<uint8_t, 8>;

struct loxone_mapping_t {
    std::string      issuerId;  // hex string, e.g. "a1b2c3d4"
    ibutton_rom_t    romCode;   // full 8-byte ROM incl. family byte and CRC
    std::string      label;     // human-readable, e.g. "Jens"
};

struct loxone_config_t {
    bool                          enabled           = true;
    uint8_t                       gpioPin           = CONFIG_LOXONE_ONEWIRE_GPIO;
    uint32_t                      activeDurationMs  = CONFIG_LOXONE_ACTIVE_DURATION_MS;
    std::vector<loxone_mapping_t> mappings;
};

} // namespace espConfig
