#pragma once
#include <array>
#include <cstdint>

// Dallas/Maxim 1-Wire CRC8
// Polynomial: x^8 + x^5 + x^4 + 1 (0x31 reversed = 0x8C)
inline uint8_t dallas_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ byte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            byte >>= 1;
        }
    }
    return crc;
}

inline bool dallas_rom_valid(const std::array<uint8_t, 8>& rom) {
    return dallas_crc8(rom.data(), 7) == rom[7];
}

inline void dallas_rom_set_crc(std::array<uint8_t, 8>& rom) {
    rom[7] = dallas_crc8(rom.data(), 7);
}
