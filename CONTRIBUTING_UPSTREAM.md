# Preparing a Pull Request to rednblkx/HomeKey-ESP32

This document tracks what needs to happen before submitting the Loxone 1-Wire feature as a PR to the upstream project. **Do not submit yet** — test on real hardware first.

## Feature scope

The PR adds an optional DS1990A iButton emulator that activates on successful HomeKey authentication, enabling direct Loxone 1-Wire integration without MQTT or WiFi.

## Files added by this fork

| File | Description |
|------|-------------|
| `main/LoxoneOneWireManager.hpp` | Class declaration |
| `main/LoxoneOneWireManager.cpp` | 1-Wire slave implementation |
| `main/include/loxone_config.hpp` | Config structs (`loxone_config_t`, `loxone_mapping_t`, `ibutton_rom_t`) |
| `main/include/dallas_crc.hpp` | Dallas CRC8 header-only utility |
| `docs/LOXONE_INTEGRATION.md` | Setup guide (German) |
| `docs/wiring/homekey-loxone-wiring.svg` | Wiring diagram |
| `flash.sh` | macOS build+flash convenience script |

## Files modified by this fork

| File | Change |
|------|--------|
| `main/Kconfig.projbuild` | Added `menu "Loxone 1-Wire Bridge"` with 4 options |
| `main/CMakeLists.txt` | Added `LoxoneOneWireManager.cpp` to `SRCS` |
| `main/include/ConfigManager.hpp` | Added `getLoxoneMappings()` / `saveLoxoneMappings()` |
| `main/ConfigManager.cpp` | NVS persistence for mapping table (namespace `lox_map`) |
| `main/include/WebServerManager.hpp` | Added 3 handler declarations |
| `main/WebServerManager.cpp` | Added `GET/POST/DELETE /loxone/mappings` routes |
| `main/main.cpp` | Instantiates `LoxoneOneWireManager`, MAC-based AP SSID |

## Pre-PR checklist

- [ ] Verified on real hardware: HomeKey tap → Loxone door open
- [ ] Tested ROM code edge cases: 7-byte input (CRC auto), 8-byte input, invalid input
- [ ] Tested mapping persistence across reboot
- [ ] Verified `portDISABLE_INTERRUPTS` timing is safe with upstream NFC activity
- [ ] Checked that `LOXONE_ONEWIRE_ENABLED=n` (disabled) compiles cleanly with zero overhead
- [ ] Reviewed upstream PR guidelines / CONTRIBUTING.md
- [ ] English-language doc added (or LOXONE_INTEGRATION.md translated)
- [ ] Rebased onto latest upstream `main`

## PR approach

The feature is fully opt-in via `CONFIG_LOXONE_ONEWIRE_ENABLED`. When disabled, no code is compiled in. This makes it safe to include in upstream without affecting existing users.

Suggested PR title: `feat: optional DS1990A iButton emulation for Loxone 1-Wire integration`

Suggested branch name (when ready): `feature/loxone-1wire`

## Upstream repository

https://github.com/rednblkx/HomeKey-ESP32
