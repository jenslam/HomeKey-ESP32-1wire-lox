# homekey-loxone

**Apple HomeKey → Loxone 1-Wire Bridge**

Fork of [rednblkx/HomeKey-ESP32](https://github.com/rednblkx/HomeKey-ESP32) with an added DS1990A iButton emulation layer. After a successful HomeKey tap, the ESP32 presents a configurable iButton ROM code on its 1-Wire bus — letting Loxone handle door access exactly as it does with physical iButtons.

> **Base project:** All HomeKey NFC authentication, HomeKit pairing, Web UI, and MQTT features come from the upstream project. See [rednblkx/HomeKey-ESP32](https://github.com/rednblkx/HomeKey-ESP32) for that documentation.

## What this fork adds

- `LoxoneOneWireManager` — DS1990A slave emulation on a configurable GPIO (default: GPIO4)
- `issuerId → ROM code` mapping table stored in NVS flash (survives reboots)
- REST API (`GET/POST/DELETE /loxone/mappings`) for managing mappings without recompiling
- `dallas_crc.hpp` — Dallas CRC8 computation and validation
- `flash.sh` — convenience script for build + flash + monitor on macOS
- `docs/LOXONE_INTEGRATION.md` — full German-language setup guide
- `docs/wiring/homekey-loxone-wiring.svg` — wiring diagram

## How it works

```
iPhone / Apple Watch → PN532 NFC → HomeKey auth (local, no internet)
  → issuerId lookup in NVS mapping table
  → GPIO4 emulates DS1990A iButton for 3 seconds
  → Loxone 1-Wire Extension reads ROM → opens door
```

**No WiFi required for operation.** HomeKey validation is fully local. Mappings are stored in flash. WiFi is only needed for initial HomeKit pairing and optional web UI access.

## Hardware

| Component | Notes |
|-----------|-------|
| ESP32 WROOM NodeMCU | Main controller |
| PN532 NFC module | SPI: SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23, SS=GPIO5 |
| 4.7 kΩ resistor | Pull-up: GPIO4 → 3.3V |
| Loxone 1-Wire Extension | DATA → GPIO4 node, GND → common ground |
| Dedicated 5V PSU | Required at installation point |

Wiring diagram: [`docs/wiring/homekey-loxone-wiring.svg`](docs/wiring/homekey-loxone-wiring.svg)

## Build & Flash

### Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) (tested with v5.x)
- Git submodules: `git submodule update --init --recursive`

### 1. Configure

```bash
idf.py menuconfig
```

Under **"Loxone 1-Wire Bridge"**:

| Option | Default | Description |
|--------|---------|-------------|
| `LOXONE_ONEWIRE_GPIO` | 4 | GPIO for 1-Wire bus |
| `LOXONE_ACTIVE_DURATION_MS` | 3000 | How long iButton is visible after tap (ms) |
| `LOXONE_MAX_MAPPINGS` | 16 | Max issuerId→ROM entries |
| `HOMEKEY_AP_PASSWORD` | `changeme1` | WiFi AP password (change this!) |

### 2. Build and flash

```bash
# macOS convenience script (auto-detects USB-serial port):
./flash.sh

# With explicit port:
./flash.sh /dev/tty.usbserial-XXXX

# Or manually:
idf.py build
idf.py -p /dev/tty.usbserial-XXXX flash
idf.py -p /dev/tty.usbserial-XXXX monitor
```

To find your port: `ls /dev/tty.*`

### 3. HomeKit pairing

On first boot the ESP32 opens a WiFi AP:
- **SSID:** `HK-Setup-XXYYZZ` (last 3 bytes of BT MAC)
- **Password:** as configured in `HOMEKEY_AP_PASSWORD`

Open `http://192.168.4.1`, enter WiFi credentials, note the HomeKit code, and pair via Apple Home. The AP never opens again automatically after pairing.

### 4. Discover your issuerId

Hold your iPhone or Apple Watch to the PN532. Serial monitor output:

```
I (XXXX) LoxoneOneWire: HomeKey tap — issuerId: a1b2c3d4
W (XXXX) LoxoneOneWire: No 1-Wire mapping for issuerId a1b2c3d4 — add via POST /loxone/mappings
```

One `issuerId` covers all devices on the same Apple ID (iPhone + Apple Watch + iPad).

### 5. Add a mapping

```bash
# Add mapping (7-byte ROM → CRC auto-calculated; or 8 bytes with CRC)
curl -X POST http://ESP32_IP/loxone/mappings \
  -H "Content-Type: application/json" \
  -d '{"issuerId":"a1b2c3d4","rom":"01A1B2C3D4E5F6","label":"Jens"}'

# List all mappings
curl http://ESP32_IP/loxone/mappings

# Delete a mapping
curl -X DELETE "http://ESP32_IP/loxone/mappings?issuerId=a1b2c3d4"
```

ROM format: 14 hex chars (7 bytes, CRC auto) or 16 hex chars (8 bytes with CRC). First byte must be `01` (DS1990A family code).

**For existing Loxone iButtons:** read the ROM code from Loxone (1-Wire Extension → iButton config) and use it directly — Loxone will see the same device it already knows.

**For new virtual iButtons:** choose any 6-byte serial, add the mapping, then run "1-Wire Search" in Loxone to discover the new device.

## Full setup guide

See [`docs/LOXONE_INTEGRATION.md`](docs/LOXONE_INTEGRATION.md) for the complete German-language guide including troubleshooting and security notes.

## License

MIT — same as upstream. See [LICENSE](LICENSE).
