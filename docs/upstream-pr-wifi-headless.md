# Upstream PR: Fix web server not starting on headless (5V PSU) boot

## Problem

When the ESP32 is powered from a 5V supply **without a USB cable attached**, the web
server (and MQTT) never start after WiFi connects. The device appears online (responds
to ping) but the web UI is unreachable indefinitely.

**Root cause — two independent races:**

### Race 1: arduino-esp32 auto-connect fires before HomeSpan registers its GOT_IP handler

1. The arduino-esp32 runtime calls `WiFi.begin()` internally during early
   initialisation, **before `setup()` runs**, using credentials stored in its own NVS
   namespace.
2. HomeSpan registers its `ARDUINO_EVENT_WIFI_STA_GOT_IP` handler inside
   `homeSpan.begin()`, which is called later during `setup()`.
3. Without a USB cable there is no `Serial.begin()` delay. On a warm NVS cache WiFi
   connects in ≈200 ms — reliably before `homeSpan.begin()` is reached.
4. The `GOT_IP` event fires before HomeSpan's handler exists. HomeSpan never calls the
   user's network callback with `status = 1`, so `webServerManager->begin()` and
   `mqttManager->begin()` are never invoked.

With USB attached, `Serial.begin(115200)` absorbs enough time that the race rarely
fires. This is why the problem only appears in production (headless) deployments.

### Race 2: HomeSpan's NVS credentials never populated (captive-portal edge case)

When the user configures WiFi through the captive portal, arduino-esp32 stores the
credentials in its own NVS namespace. HomeSpan stores credentials in a separate NVS
namespace. If the captive-portal session completes but HomeSpan's `setWifiCredentials()`
call was never reached (e.g. power loss, interrupted reboot), HomeSpan finds empty
credentials on the next boot and enters AP mode again — despite WiFi working fine.

### Additional: GPIO3 (UART0 RX) floats without USB on CH340 boards

The CH340 USB-UART bridge is only powered when USB is connected. Without USB, GPIO3
floats and generates spurious UART input that can confuse HomeSpan's serial command
parser (e.g. triggering `processSerialCommand("A")` which starts AP mode).

### Additional: lambda(0) blocks `loop()` indefinitely

The existing AP-mode callback (`status == 0`) ended with `while(true) { vTaskDelay(); }`.
This permanently blocked `loop()`, preventing HomeSpan from processing any future events
after a re-connection attempt.

---

## Fix

Four coordinated changes in `main.cpp`, one in `WebServerManager.cpp`, and two in
the HomeSpan submodule (`HomeSpan.cpp`).

### 1. Hard-stop WiFi before HomeSpan init (eliminates Race 1)

```cpp
void setup() {
  // Read arduino-esp32 NVS credentials BEFORE turning WiFi off
  char bridgeSSID[33] = {0};
  char bridgePSK[65]  = {0};
  WiFi.mode(WIFI_STA);
  {
    wifi_config_t cfg = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK
        && strlen((char*)cfg.sta.ssid) > 0) {
      strncpy(bridgeSSID, (char*)cfg.sta.ssid,     32);
      strncpy(bridgePSK,  (char*)cfg.sta.password, 64);
    }
  }

  // Cancel any in-progress auto-connect so HomeSpan can register
  // its GOT_IP handler before WiFi reconnects.
  WiFi.disconnect(true);   // true = also turn off WiFi driver
  WiFi.mode(WIFI_OFF);
  vTaskDelay(pdMS_TO_TICKS(100));

  Serial.begin(115200);
  // ... rest of setup ...
```

`WiFi.disconnect(true)` shuts the WiFi driver down entirely. HomeSpan's
`homeSpan.begin()` (called via `homekitLock->begin()`) re-initialises WiFi through its
own stack and registers the GOT_IP handler before any reconnect can occur.

### 2. Bridge arduino-esp32 credentials into HomeSpan's NVS (eliminates Race 2)

```cpp
  // After all managers are created, before homekitLock->begin():
  if (strlen(bridgeSSID) > 0) {
    homeSpan.setWifiCredentials(bridgeSSID, bridgePSK);
  }
  homekitLock->begin();  // homeSpan.begin() is called inside here
```

`setWifiCredentials()` writes into HomeSpan's NVS namespace. Called before
`homeSpan.begin()`, it ensures HomeSpan finds credentials on first boot and skips AP
mode even if the captive-portal reboot was interrupted.

### 3. Independent GOT_IP fallback handler + `webStarted` guard

Even with the WiFi hard-stop, HomeSpan can still miss a GOT_IP event on very fast
hardware. A second arduino handler fires regardless:

```cpp
static bool webStarted = false;

static void startWebServer() {
  if (webStarted) return;          // guard: whichever fires first wins
  webStarted = true;
  char identifier[18];
  sprintf(identifier, "%.2s%.2s%.2s%.2s%.2s%.2s",
      HAPClient::accessory.ID,      HAPClient::accessory.ID + 3,
      HAPClient::accessory.ID + 6,  HAPClient::accessory.ID + 9,
      HAPClient::accessory.ID + 12, HAPClient::accessory.ID + 15);
  mqttManager->begin(std::string(identifier));
  webServerManager->begin();
}

// HomeSpan callback (status == 1) now calls startWebServer() instead of inline code.
// Additional arduino fallback — fires even if HomeSpan missed the event:
WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
  startWebServer();
  pollHS = true;
}, ARDUINO_EVENT_WIFI_STA_GOT_IP);
```

### 4. Fix GPIO3 float on CH340 boards

```cpp
void setup() {
  gpio_set_pull_mode(GPIO_NUM_3, GPIO_PULLUP_ONLY);  // first line — before Serial
```

Idempotent on non-CH340 boards. No effect on ESP32-S3 (native USB).

### 5. Fix blocking `while(true)` in AP callback

```cpp
  // Before:
  webServerManager->begin();
  while(true) { vTaskDelay(pdMS_TO_TICKS(100)); }  // ← blocks loop() forever

  // After:
  webServerManager->begin();
  // Return immediately. loop() keeps running; HAP resumes after reboot with credentials.
```

### 6. AP SSID collision fix + configurable password (Kconfig)

The hardcoded AP SSID `"HomeKey-ESP32"` collides when multiple devices are on the same
network. The hardcoded password `"homekey123"` is a security issue.

```cpp
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  char apSsid[24];
  snprintf(apSsid, sizeof(apSsid), "HK-Setup-%02X%02X%02X", mac[3], mac[4], mac[5]);
  WiFi.softAP(apSsid, CONFIG_HOMEKEY_AP_PASSWORD, ...);
```

`CONFIG_HOMEKEY_AP_PASSWORD` is defined in `Kconfig.projbuild` with default
`"changeme1"` and a help text prompting the user to change it before first flash.

### 7. HomeSpan event queue: size 10→30 + drain all events per poll()

Two changes to `HomeSpan.cpp` (HomeSpan submodule):

```cpp
// Before:
networkEventQueue = xQueueCreate(10, sizeof(arduino_event_t));
// After:
networkEventQueue = xQueueCreate(30, sizeof(arduino_event_t));  // handle burst on headless boot

// Before:
if (xQueueReceive(networkEventQueue, &event, (TickType_t)0))
    networkCallback(event);
// After:
while (xQueueReceive(networkEventQueue, &event, (TickType_t)0))  // drain all queued events
    networkCallback(event);
```

On a headless boot multiple WiFi events (STA_START, STA_CONNECTED, GOT_IP, …) arrive
in rapid succession. With `xQueueCreate(10, …)` and `if` (process one per poll cycle),
events at the tail of a burst can be lost before `poll()` has a chance to drain them.
Increasing to 30 and switching to `while` ensures all events are processed within a
single `loop()` iteration.

### 8. WebServerManager: raise max_uri_handlers + pre-connection log buffer

```cpp
// Before:
ssl_config.httpd.max_uri_handlers = 22;
// After:
ssl_config.httpd.max_uri_handlers = 32;  // was silently dropping route registrations
```

The project registers more than 22 URI handlers. `httpd_register_uri_handler` silently
fails when the table is full, causing some routes to be unreachable.

```cpp
// Before:
static const size_t max_buffer = 64;
// After:
static const size_t max_buffer = 512;  // retain boot logs until first WS client connects
```

---

## Affected configurations

| Configuration | Before | After |
|--------------|--------|-------|
| USB connected | Works | Works |
| 5V PSU, warm NVS, fast WiFi | Web UI unreachable | Works |
| 5V PSU, HomeSpan NVS empty | Loops in AP mode | Works |
| CH340 board, no USB | Spurious serial input possible | Works |
| Multiple devices on same network | AP SSID collision | Unique per device |

## Tested on

- ESP32 WROOM NodeMCU (CH340), 5V 3A PSU via Vin, no USB attached
- ESP-IDF v5.5.4, arduino-esp32 3.3.8
- HomeSpan 2.1.1 (as vendored in this repo)
