#include <algorithm>
#include <cstdint>
#include <memory>
#include "ConsoleLogSinker.h"
#include "HomeSpan.h"
#include "config.hpp"
#include <esp_event.h>
#include "eth_structs.hpp"
#include "dns_server.h"
#include "HomeKitLock.hpp"
#include "LockManager.hpp"
#include "NfcManager.hpp"
#include "ConfigManager.hpp"
#include "ReaderDataManager.hpp"
#include "HardwareManager.hpp"
#include "MqttManager.hpp"
#include "WebServerManager.hpp"
#include "LoxoneOneWireManager.hpp"
#include <functional>
#include <sodium/crypto_sign.h>
#include <sodium/crypto_box.h>
#include "HAP.h"
#include "loggable.hpp"
#include "loggable_espidf.hpp"
#include "WebSocketLogSinker.h"
#include "lwip/inet.h"
#include <esp_wifi.h>

std::unique_ptr<LockManager> lockManager;
std::unique_ptr<ReaderDataManager> readerDataManager;
std::unique_ptr<ConfigManager> configManager;
std::unique_ptr<HardwareManager> hardwareManager;
std::unique_ptr<MqttManager> mqttManager;
std::unique_ptr<WebServerManager> webServerManager;
std::unique_ptr<HomeKitLock> homekitLock;
std::unique_ptr<NfcManager> nfcManager;
std::unique_ptr<LoxoneOneWireManager> loxoneManager;

static dns_server_handle_t dns_server = NULL;

bool pollHS = false;

static void dhcp_set_captiveportal_url(void) {
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI("Main", "Setting up captive portal on IP: %s", ip_addr);

    char captiveportal_uri[32];
    snprintf(captiveportal_uri, sizeof(captiveportal_uri), "http://%s", ip_addr);

    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, captiveportal_uri, strlen(captiveportal_uri)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));
}

static void start_captive_portal(void)
{
    dhcp_set_captiveportal_url();

    dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    dns_server = start_dns_server(&dns_config);
    ESP_LOGI("Main", "DNS server started for captive portal");
}

static bool webStarted = false;

static void startWebServer() {
  if (webStarted) return;
  webStarted = true;
  char identifier[18];
  sprintf(identifier, "%.2s%.2s%.2s%.2s%.2s%.2s", HAPClient::accessory.ID, HAPClient::accessory.ID + 3, HAPClient::accessory.ID + 6, HAPClient::accessory.ID + 9, HAPClient::accessory.ID + 12, HAPClient::accessory.ID + 15);
  mqttManager->begin(std::string(identifier));
  webServerManager->begin();
}

std::function<void(int)> lambda = [](int status) {
  if (status == 1) {
    startWebServer();
  } else if (status == 0){
    pollHS = false;
    mqttManager->end();
    webServerManager->end();
    WiFi.mode(WIFI_AP_STA);
    // SSID includes last 3 MAC bytes to avoid collisions; password set via menuconfig
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    char apSsid[24];
    snprintf(apSsid, sizeof(apSsid), "HK-Setup-%02X%02X%02X", mac[3], mac[4], mac[5]);
    WiFi.softAP(apSsid, CONFIG_HOMEKEY_AP_PASSWORD, 11, false, 2, false, WIFI_AUTH_WPA2_WPA3_PSK, WIFI_CIPHER_TYPE_AES_CMAC128);
    start_captive_portal();
    webServerManager->begin();
    // Return immediately — do not block setup(). HAP starts after WiFi credentials are
    // saved via captive portal and device reboots.
  }
};
using namespace loggable;

/**
 * @brief Initialize runtime, configure logging/serial, and instantiate core subsystem managers.
 *
 * Initializes the global runtime infrastructure (Sinker), sets logging levels and Serial,
 * constructs and assigns global manager instances (ReaderDataManager, ConfigManager, WebServerManager,
 * HardwareManager, LockManager, MqttManager, HomeKitLock, NfcManager), reads NFC-related configuration,
 * and starts managers that require explicit startup.
 *
 * @note This function allocates and assigns globals used across the application and invokes their
 *       initialization routines (calls to `begin()` where applicable). It also logs the resolved NFC
 *       GPIO pin configuration based on persisted settings.
 */
void setup() {
  gpio_set_pull_mode(GPIO_NUM_3, GPIO_PULLUP_ONLY);  // UART0 RX idle-HIGH without CH340

  // Read stored WiFi credentials from arduino-esp32 NVS before turning off WiFi.
  // Used to bridge into HomeSpan's wifiNVS if it was never populated (e.g. captive-portal
  // session was interrupted before homeSpan.setWifiCredentials() could be called).
  char bridgeSSID[33] = {0};
  char bridgePSK[65] = {0};
  WiFi.mode(WIFI_STA);  // ensure WiFi driver is initialised so esp_wifi_get_config() works
  {
    wifi_config_t wifiStaCfg = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &wifiStaCfg) == ESP_OK && strlen((char*)wifiStaCfg.sta.ssid) > 0) {
      strncpy(bridgeSSID, (char*)wifiStaCfg.sta.ssid, 32);
      strncpy(bridgePSK, (char*)wifiStaCfg.sta.password, 64);
    }
  }

  // Stop WiFi entirely so any in-progress auto-connect from NVS is cancelled before HomeSpan
  // registers its GOT_IP handler. HomeSpan.begin() will re-enable WiFi through its own init.
  WiFi.disconnect(true);    // disconnect + turn off WiFi module
  WiFi.mode(WIFI_OFF);
  vTaskDelay(pdMS_TO_TICKS(100));
  Serial.begin(115200);
  loggable::espidf::LogHook::install(false, true);
  Sinker::instance().add_sinker(std::make_shared<loggable::ConsoleLogSinker>());
  esp_err_t err = esp_event_loop_create_default();
  if (err != ESP_OK) {
    ESP_LOGE("Main", "Failed to create default event loop: %d", err);
  }
  readerDataManager = std::make_unique<ReaderDataManager>();
  configManager = std::make_unique<ConfigManager>();
  configManager->begin();
  esp_log_level_set("*", static_cast<esp_log_level_t>(configManager->getConfig<espConfig::misc_config_t>().logLevel));
  loggable::Sinker::instance().set_level((loggable::LogLevel)configManager->getConfig<espConfig::misc_config_t>().logLevel);
  webServerManager = std::make_unique<WebServerManager>(*configManager, *readerDataManager);
  Sinker::instance().add_sinker(std::make_shared<loggable::WebSocketLogSinker>(webServerManager.get()));
  hardwareManager = std::make_unique<HardwareManager>(configManager->getConfig<espConfig::actions_config_t>());
  lockManager = std::make_unique<LockManager>(configManager->getConfig<espConfig::misc_config_t>(), configManager->getConfig<espConfig::actions_config_t>());

  // Loxone 1-Wire bridge
  loxoneManager = std::make_unique<LoxoneOneWireManager>(
      configManager->getConfig<espConfig::loxone_config_t>());
  loxoneManager->begin();

  mqttManager = std::make_unique<MqttManager>(*configManager);
  homekitLock = std::make_unique<HomeKitLock>(lambda, *lockManager, *configManager, *readerDataManager);
  espConfig::misc_config_t miscConfig = configManager->getConfig<espConfig::misc_config_t>();
  static const char* TAG = "Main";
  if(miscConfig.nfcPinsPreset != PIN_UNSET){
    ESP_LOGI(TAG, "NFC GPIO pins preset: %s", nfcGpioPinsPresets[miscConfig.nfcPinsPreset].name.c_str());
    ESP_LOGI(TAG, "NFC preset pins: %d, %d, %d, %d", nfcGpioPinsPresets[miscConfig.nfcPinsPreset].gpioPins[0], nfcGpioPinsPresets[miscConfig.nfcPinsPreset].gpioPins[1], nfcGpioPinsPresets[miscConfig.nfcPinsPreset].gpioPins[2], nfcGpioPinsPresets[miscConfig.nfcPinsPreset].gpioPins[3]);
  } else {
    ESP_LOGI(TAG, "NFC GPIO pins preset: Custom");
    ESP_LOGI(TAG, "NFC Custom GPIO pins: %d, %d, %d, %d", miscConfig.nfcGpioPins[0], miscConfig.nfcGpioPins[1], miscConfig.nfcGpioPins[2], miscConfig.nfcGpioPins[3]);
  }
  ESP_LOGI(TAG, "NFC reader type: %s", miscConfig.nfcReaderType == 0 ? "PN532" : "PN7160");
  if (miscConfig.nfcReaderType == 1) {
    ESP_LOGI(TAG, "NFC IRQ pin: %d, VEN pin: %d", miscConfig.nfcIrqPin, miscConfig.nfcVenPin);
  }
  readerDataManager->begin();
  if(miscConfig.ethernetEnabled){
    std::vector<uint8_t> ethPins;
    if(miscConfig.ethActivePreset == PIN_UNSET){
      ethPins = {miscConfig.ethSpiConfig[4], miscConfig.ethSpiConfig[5], miscConfig.ethSpiConfig[6]};
    } else { 
        const eth_board_presets_t& ethPreset = eth_config_ns::boardPresets[miscConfig.ethActivePreset];
        ethPins = {ethPreset.spi_conf.pin_sck, ethPreset.spi_conf.pin_miso, ethPreset.spi_conf.pin_mosi};
    }
    std::vector<uint8_t> nfcPins;
    if(miscConfig.nfcPinsPreset == PIN_UNSET){
      nfcPins = {miscConfig.nfcGpioPins[1], miscConfig.nfcGpioPins[2], miscConfig.nfcGpioPins[3]};
    } else {
      nfcPins = {nfcGpioPinsPresets[miscConfig.nfcPinsPreset].gpioPins[1], nfcGpioPinsPresets[miscConfig.nfcPinsPreset].gpioPins[2], nfcGpioPinsPresets[miscConfig.nfcPinsPreset].gpioPins[3]};
    }
    std::vector<uint8_t> pins_intersection;
    std::ranges::set_intersection(ethPins.begin(), ethPins.end(), nfcPins.begin(), nfcPins.end(), std::back_inserter(pins_intersection));
    if((!pins_intersection.empty() && miscConfig.ethSpiBus == SPI2_HOST)){
      goto nfc_init;
    }
    #if SOC_SPI_PERIPH_NUM > 2
    else if (pins_intersection.empty() && miscConfig.ethSpiBus == SPI3_HOST) goto nfc_init;
    #endif
    else {
      if(miscConfig.ethSpiBus == SPI2_HOST){
        ESP_LOGE(TAG, "Ethernet enabled on SPI2 Bus, NFC reader has to use the same GPIO pins as Ethernet");
      } else {
        ESP_LOGE(TAG, "Ethernet enabled on SPI3 Bus, NFC reader cannot use the same GPIO pins as Ethernet");
        for(auto& pin : pins_intersection){
          ESP_LOGE(TAG, "GPIO Intersection: %d", pin);
        }
      }
    }
  } else {
  nfc_init:
    nfcManager = std::make_unique<NfcManager>(*readerDataManager,
                                miscConfig.nfcPinsPreset == PIN_UNSET ? miscConfig.nfcGpioPins : nfcGpioPinsPresets[miscConfig.nfcPinsPreset].gpioPins,
                                miscConfig.nfcReaderType,
                                miscConfig.nfcIrqPin,
                                miscConfig.nfcVenPin,
                                miscConfig.hkAuthPrecomputeEnabled,
                                miscConfig.nfcFastPollingEnabled);
    nfcManager->begin();
  }
  webServerManager->setNfcManager(nfcManager.get());
  webServerManager->setMqttManager(mqttManager.get());
  // Loxone REST routes are registered in WebServerManager::setupRoutes()
  // called during webServerManager->begin() below
  hardwareManager->begin();

  // Bridge arduino WiFi credentials into HomeSpan's wifiNVS if HomeSpan has none.
  // homeSpan.begin() (inside homekitLock->begin()) writes the struct to NVS and skips
  // AP mode when the struct is populated — so we must seed it BEFORE begin().
  if (strlen(bridgeSSID) > 0) {
    homeSpan.setWifiCredentials(bridgeSSID, bridgePSK);
    ESP_LOGI("Main", "Seeded HomeSpan WiFi from arduino NVS: %s", bridgeSSID);
  }

  homekitLock->begin();
  lockManager->begin();
  WiFi.onEvent([](arduino_event_id_t event){
    static uint8_t count = 0;
    if(count >= 6){
      homeSpan.processSerialCommand("A");
      count = 0;
    } else {
      count++;
    }
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // Fallback: start webserver + re-enable HAP polling if HomeSpan missed GOT_IP (headless boot)
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info){
    startWebServer();
    pollHS = true;
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

  // Watchdog: restart if WiFi never connects within 90s of boot.
  // Skip restart in AP/APSTA mode — device is awaiting credentials via captive portal.
  xTaskCreate([](void*) {
    vTaskDelay(pdMS_TO_TICKS(90000));
    wifi_mode_t wdMode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&wdMode);
    if (wdMode == WIFI_MODE_STA && !WiFi.isConnected()) {
      ESP_LOGI("WiFiWatchdog", "No WiFi after 90s in STA mode — restarting");
      esp_restart();
    }
    vTaskDelete(nullptr);
  }, "wifi_wd", 2048, nullptr, 1, nullptr);

  pollHS = true;
}

/**
 * @brief Run the main application loop: service HomeSpan events and yield to the RTOS.
*
 * Polls HomeSpan to process HomeKit and internal events, then delays 50 ms to allow other
 * FreeRTOS tasks to run.
 */

void loop() {
  if(pollHS)
    homeSpan.poll();
  vTaskDelay(pdMS_TO_TICKS(50));
}
