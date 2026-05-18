# Upstream PR Proposal: Fix web server not starting in headless (5V PSU) operation

## Problem

When the ESP32 is powered from a 5V supply without a USB cable, the web server (and MQTT)
never start after WiFi connects. The device appears online (responds to ping) but the
web UI is unreachable.

**Root cause:** a race condition between the Arduino ESP32 WiFi stack and HomeSpan's
internal event registration.

1. On boot with stored credentials in NVS, `WiFi.begin()` is called internally by
   `arduino-esp32` during early initialisation — before `setup()` runs.
2. HomeSpan registers its `ARDUINO_EVENT_WIFI_STA_GOT_IP` handler inside `homeSpan.begin()`,
   which is called later in `setup()`.
3. When WiFi connects fast enough (common on a warm NVS cache, no USB serial delay),
   the `GOT_IP` event fires *before* HomeSpan has registered its handler.
4. HomeSpan never calls the user's WiFi status callback with `status = 1`, so
   `mqttManager->begin()` and `webServerManager->begin()` are never invoked.

Without a USB cable there is no Serial initialisation delay, so this race is
**reliably triggered** in headless operation. With USB the Serial.begin() overhead
absorbs enough time that the race rarely fires.

## Fix (two lines + one function)

**1. Prevent auto-connect before HomeSpan registers its handler**

```cpp
void setup() {
  WiFi.disconnect(false);       // drop any in-progress connection from boot
  WiFi.setAutoReconnect(false); // HomeSpan re-enables this after homeSpan.begin()
  Serial.begin(115200);
  // ... rest of setup ...
  homekitLock->begin();  // calls homeSpan.begin() — registers GOT_IP handler
  lockManager->begin();
  // NOW safe to reconnect — HomeSpan's handler is in place
}
```

**2. Add an independent GOT_IP handler as fallback**

HomeSpan's handler calls the user lambda correctly when it catches the event.
The fallback below is a safety net for the case where HomeSpan still misses it
(e.g. first-boot timing) and ensures `startWebServer()` is called exactly once
regardless of which handler fires first:

```cpp
static bool webStarted = false;

static void startWebServer() {
  if (webStarted) return;   // guard against duplicate calls
  webStarted = true;
  char identifier[18];
  sprintf(identifier, "%.2s%.2s%.2s%.2s%.2s%.2s",
    HAPClient::accessory.ID,     HAPClient::accessory.ID + 3,
    HAPClient::accessory.ID + 6, HAPClient::accessory.ID + 9,
    HAPClient::accessory.ID + 12, HAPClient::accessory.ID + 15);
  mqttManager->begin(std::string(identifier));
  webServerManager->begin();
}

// In setup(), after homekitLock->begin():
WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
  startWebServer();
}, ARDUINO_EVENT_WIFI_STA_GOT_IP);
```

The existing HomeSpan lambda is left unchanged; it now calls the same guarded
`startWebServer()` instead of the inline code. The guard ensures the second
caller (whichever that is) is a no-op.

**3. Bonus: fix UART RX floating without USB (NodeMCU / CH340 boards)**

The CH340 USB-UART chip is only powered when USB is connected. Without USB,
GPIO3 (UART0 RX) floats, causing spurious UART input that can confuse
HomeSpan's serial command parser:

```cpp
gpio_set_pull_mode(GPIO_NUM_3, GPIO_PULLUP_ONLY);  // must be first line in setup()
```

This is idempotent on boards that don't use CH340 and has no effect on ESP32-S3
(built-in USB).

## Affected configurations

| Config | Before fix | After fix |
|--------|-----------|-----------|
| USB connected | Works (Serial delay absorbs race) | Works |
| 5V PSU, warm NVS | Web UI unreachable | Works |
| 5V PSU, first boot | AP mode starts correctly | AP mode starts correctly |
| arduino-esp32 ≥ 3.3.8 | Race less frequent but still possible | Works |

## Tested on

- ESP32 WROOM NodeMCU with CH340, 5V 3A PSU via Vin
- ESP-IDF v5.5.4, arduino-esp32 3.3.8

## Notes

- `WiFi.disconnect(false)` with `false` preserves stored credentials; it only
  drops any in-progress association so HomeSpan can re-initiate it cleanly.
- `WiFi.setAutoReconnect(false)` is re-enabled by HomeSpan internally after
  `homeSpan.begin()` completes.
- The 90s watchdog task (restarts if WiFi never connects) is an optional
  companion change for truly headless deployments where a stale credential
  would otherwise leave the device unreachable indefinitely.
