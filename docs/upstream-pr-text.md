# PR Title
fix: reliable startup on headless (5V PSU, no USB) boot

---

# PR Description (paste into GitHub)

## Problem

When the ESP32 is powered from a 5V supply **without a USB cable**, the web server and
MQTT never start after WiFi connects. The device is reachable by ping but the web UI is
unreachable indefinitely. This is a production-only bug — USB users don't see it because
`Serial.begin()` absorbs enough time to mask the race.

**Root cause:** arduino-esp32 auto-connects using NVS credentials before `setup()` runs.
HomeSpan registers its `GOT_IP` handler inside `homeSpan.begin()`, which is called later.
On a headless boot WiFi connects in ~200 ms, reliably before `homeSpan.begin()` is
reached. The `GOT_IP` event is lost, `webServerManager->begin()` is never called.

Two secondary issues found during debugging:
- If the captive-portal session was interrupted before reboot, HomeSpan's NVS is empty
  while arduino-esp32's NVS has valid credentials → device loops in AP mode forever.
- The existing AP callback (`status == 0`) blocked `loop()` with `while(true)`, freezing
  HAP event processing permanently.

## Changes

**`main/main.cpp`**
- Call `WiFi.disconnect(true)` + `WiFi.mode(WIFI_OFF)` at the very top of `setup()`
  before `Serial.begin()`. This cancels any in-progress auto-connect so HomeSpan can
  register its `GOT_IP` handler before WiFi reconnects.
- Read arduino-esp32 NVS credentials before shutting WiFi off, then call
  `homeSpan.setWifiCredentials()` before `homeSpan.begin()`. Bridges the edge case
  where HomeSpan's NVS was never written (interrupted captive-portal session).
- Extract `startWebServer()` with a `webStarted` guard. Both the HomeSpan callback and
  a new independent `ARDUINO_EVENT_WIFI_STA_GOT_IP` handler call it — whichever fires
  first wins, the other is a no-op.
- Remove `while(true)` from the AP callback so `loop()` keeps running.
- Derive AP SSID from BT MAC bytes (`HK-Setup-XXYYZZ`) to avoid collisions.
- Add 90s watchdog: restart if WiFi never connects in STA mode (skips restart in AP/APSTA
  mode to not interrupt a captive-portal session).
- `gpio_set_pull_mode(GPIO_NUM_3, GPIO_PULLUP_ONLY)` as first line: keeps UART0 RX
  high without CH340 powered, preventing spurious serial input on headless boards.

**`main/WebServerManager.cpp`**
- `max_uri_handlers` 22 → 32: was silently dropping route registrations when the table
  was full.
- Pre-connection WebSocket broadcast buffer 64 → 512: retains boot log messages until
  the first browser connects.

**`main/Kconfig.projbuild`**
- Add `CONFIG_HOMEKEY_AP_PASSWORD` (default `"changeme1"`) so the setup AP password is
  configurable via menuconfig instead of hardcoded.

**`components/HomeSpan/upstream` (submodule)**
- `networkEventQueue` size 10 → 30: prevents overflow of burst events on headless boot.
- `if (xQueueReceive(...))` → `while (xQueueReceive(...))`: drain all queued events per
  `poll()` call instead of one per `loop()` iteration.

## Testing

Tested on ESP32 WROOM NodeMCU (CH340), 5V 3A PSU via Vin, no USB cable attached,
ESP-IDF v5.5.4, arduino-esp32 3.3.8. Web UI, MQTT, and HomeKit all start reliably on
every boot.

Full technical write-up: [`docs/upstream-pr-wifi-headless.md`](docs/upstream-pr-wifi-headless.md)

Branch: `MadeInHelpup/HomeKey-ESP32-1wire-lox:fix/headless-webserver`
