# homekey-loxone

**Apple HomeKey → Loxone 1-Wire Bridge**

Fork of [rednblkx/HomeKey-ESP32](https://github.com/rednblkx/HomeKey-ESP32) with an added DS1990A iButton emulation layer. After a successful HomeKey tap, the ESP32 presents a virtual iButton ROM code on its 1-Wire bus — letting Loxone handle door access exactly as it does with physical iButtons.

> **Base project:** All HomeKey NFC authentication, HomeKit pairing, Web UI, and MQTT features come from the upstream project. See [rednblkx/HomeKey-ESP32](https://github.com/rednblkx/HomeKey-ESP32) for that documentation.

## What this fork adds

- `LoxoneOneWireManager` — DS1990A slave emulation on a configurable GPIO (default: GPIO27)
- ROM code auto-derived from HomeKey `issuerId` — no manual mapping table needed
- `dallas_crc.hpp` — Dallas CRC8 computation and validation
- Loxone settings page in the Web UI (enable/disable, GPIO, active window duration)
- `flash.sh` — convenience script for build + flash + monitor on macOS
- `docs/LOXONE_INTEGRATION.md` — full German-language setup guide
- `docs/wiring/homekey-loxone-wiring.svg` — wiring diagram

## How it works

```
iPhone / Apple Watch → PN532 NFC → HomeKey auth (local, no internet)
  → ROM derived from issuerId: [0x01, issuerId[0..5], CRC8]
  → GPIO27 emulates DS1990A iButton for 3 seconds (configurable)
  → timer expires → iButton disappears from bus (0→1→0 state change)
  → Loxone 1-Wire Extension detects state change → Zugangs-Baustein triggers
```

**No WiFi required for operation.** HomeKey validation is fully local. ROM derivation is deterministic — same Apple ID always produces the same ROM. WiFi is only needed for initial HomeKit pairing and optional web UI access.

## Hardware

| Component | Notes |
|-----------|-------|
| ESP32 WROOM NodeMCU | Main controller |
| PN532 NFC module | SPI: SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23, SS=GPIO5 |
| 4.7 kΩ resistor | Pull-up: GPIO27 → 3.3V |
| 100 µF electrolytic cap | Across 3.3V / GND pins — stabilises regulator under WiFi load |
| Loxone 1-Wire Extension | DATA → GPIO27 node, GND → common ground |
| Dedicated 5V PSU | Required at installation point |

Wiring diagram: [`docs/wiring/homekey-loxone-wiring.svg`](docs/wiring/homekey-loxone-wiring.svg)

> **NodeMCU + 5V PSU known issue:** NodeMCU boards powered via Vin (not USB) may fail to connect to WiFi due to an old arduino-esp32 Serial initialisation behaviour. This fork pins arduino-esp32 to ≥3.3.8 which resolves the issue. See [ESPresense/ESPresense#1131](https://github.com/ESPresense/ESPresense/issues/1131) for background.

## Build & Flash

### Prerequisites

- [ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/get-started/)
- [bun](https://bun.sh) (for Svelte web UI build)
- Git submodules: `git submodule update --init --recursive`

### 1. Configure

```bash
idf.py menuconfig
```

Under **"Loxone 1-Wire Bridge"**:

| Option | Default | Description |
|--------|---------|-------------|
| `LOXONE_ONEWIRE_GPIO` | 27 | GPIO for 1-Wire bus (must support open-drain) |
| `LOXONE_ACTIVE_DURATION_MS` | 3000 | How long iButton is visible after tap (ms) |
| `HOMEKEY_AP_PASSWORD` | `changeme1` | WiFi AP password — **change before first flash** |

Runtime changes can be made via the **Loxone** page in the Web UI (saved to NVS).

### 2. Build and flash

```bash
export PATH="$HOME/.bun/bin:$PATH"
. ~/esp/esp-idf/export.sh

# macOS convenience script (auto-detects USB-serial port):
./flash.sh

# Or manually:
idf.py -p /dev/tty.SLAB_USBtoUART flash
```

To find your port: `ls /dev/tty.*`

### 3. HomeKit pairing

On first boot the ESP32 opens a WiFi AP:
- **SSID:** `HK-Setup-XXYYZZ` (last 3 bytes of BT MAC)
- **Password:** as configured in `HOMEKEY_AP_PASSWORD`

Open `http://192.168.4.1`, enter WiFi credentials, note the HomeKit code, and pair via Apple Home. The AP never opens again automatically after pairing.

### 4. Discover your ROM code

Hold your iPhone or Apple Watch to the PN532. The serial monitor (and system log in Web UI) shows:

```
I (XXXX) LoxoneOneWire: HomeKey tap — issuerId: f2963548...
I (XXXX) LoxoneOneWire: issuerId: f2963548... → ROM: 01F29635484B2E48AA
I (XXXX) LoxoneOneWire: Add to Loxone (if new): 01F29635484B2E48AA
```

One `issuerId` covers all devices on the same Apple ID (iPhone + Apple Watch + iPad).

### 5. Register in Loxone

1. In Loxone Config: open the 1-Wire Extension, run **"1-Wire Suche"** while tapping your phone to the reader
2. The virtual iButton appears in the search results
3. Assign it to a **Zugangs-Baustein** (access controller block)

The ROM code is deterministic — same Apple ID always produces the same ROM, so the Loxone mapping survives ESP32 reflashing.

## Full setup guide

See [`docs/LOXONE_INTEGRATION.md`](docs/LOXONE_INTEGRATION.md) for the complete German-language guide including troubleshooting and security notes.

## License

MIT — same as upstream. See [LICENSE](LICENSE).
