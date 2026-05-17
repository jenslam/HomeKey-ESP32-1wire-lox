#include "LoxoneOneWireManager.hpp"
#include "dallas_crc.hpp"
#include "eventStructs.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include <serialization.hpp>
#include <span>

LoxoneOneWireManager::LoxoneOneWireManager(const espConfig::loxone_config_t& config)
    : m_config(config), m_gpio(static_cast<gpio_num_t>(config.gpioPin)) {}

LoxoneOneWireManager::~LoxoneOneWireManager() {
    if (m_taskHandle) vTaskDelete(m_taskHandle);
    if (m_resetSem)   vSemaphoreDelete(m_resetSem);
    gpio_isr_handler_remove(m_gpio);
}

void LoxoneOneWireManager::begin() {}

void LoxoneOneWireManager::activateRom(const espConfig::ibutton_rom_t& rom) {}

void IRAM_ATTR LoxoneOneWireManager::handleEdge() {}

void LoxoneOneWireManager::owTask() {}

void IRAM_ATTR LoxoneOneWireManager::isrHandler(void* arg) {
    static_cast<LoxoneOneWireManager*>(arg)->handleEdge();
}

void LoxoneOneWireManager::taskEntry(void* arg) {
    static_cast<LoxoneOneWireManager*>(arg)->owTask();
    vTaskDelete(nullptr);
}

void     LoxoneOneWireManager::sendPresence()           {}
uint8_t  LoxoneOneWireManager::receiveByte()            { return 0; }
void     LoxoneOneWireManager::sendByte(uint8_t)        {}
void     LoxoneOneWireManager::sendBit(bool)            {}
bool     LoxoneOneWireManager::receiveBit()             { return false; }
void     LoxoneOneWireManager::handleReadRom()          {}
void     LoxoneOneWireManager::handleSearchRom()        {}
