#pragma once

#include "NfcReader.hpp"
#include "pn7160.hpp"
#include "portmacro.h"
#include "spi.hpp"

#include <array>
#include <memory>
#include <vector>

/**
 * @brief PN7160 SPI implementation of the INfcReader interface.
 *
 * Wraps PN7160_SPI transport and PN7160_NCI driver, managing the background
 * task_runner task and event-driven tag discovery.
 */
class Pn7160Reader : public INfcReader {
public:
    Pn7160Reader(const std::array<uint8_t, 4>& gpioPins,
                 uint8_t irqPin,
                 uint8_t venPin,
                 const std::array<uint8_t, 18>& ecpData);
    ~Pn7160Reader() override;
		Pn7160Reader(const Pn7160Reader&) = delete;
		Pn7160Reader& operator=(const Pn7160Reader&) = delete;
		Pn7160Reader(Pn7160Reader&&) = delete;

    bool init() override;
    void stop() override;
    bool isConnected() const override;

    uint8_t getFwMajor() const override { return m_fwMajor; }
    uint8_t getFwMinor() const override { return m_fwMinor; }

    bool beginDiscovery() override;
    bool pollForTag(std::vector<uint8_t>& uid,
                    std::array<uint8_t, 2>& atqa,
                    uint8_t& sak,
                    uint32_t timeoutMs) override;
    bool isTagStillPresent() override;
    void releaseTag() override;
    void endDiscovery() override;

    bool updateECP() override;
    bool exchangeApdu(const std::vector<uint8_t>& send,
                      std::vector<uint8_t>& recv,
                      uint32_t timeoutMs) override;
    bool healthCheck() override;
    
private:
    const std::array<uint8_t, 18> &m_ecpData;
    std::array<uint8_t, 4> m_gpioPins;
    uint8_t m_irqPin;
    uint8_t m_venPin;

    std::unique_ptr<PN7160_SPI> m_transport;
    std::unique_ptr<PN7160_NCI> m_nci;

    bool m_connected = false;
    uint32_t m_lastPresenceCheck = 0; // Tick count of last rf_iso_dep_presence_check()
    TickType_t m_lastHealthCheckTick = 0;
    uint8_t m_fwMajor = 0;
    uint8_t m_fwMinor = 0;
    uint8_t m_currentProtocol = 0;    // RF protocol of the active tag (nci::PROT_*)
    TickType_t m_lastActivation = 0;

    static constexpr uint32_t kPresenceCheckIntervalMs = 500;
    static constexpr uint32_t kHealthCheckIntervalMs = 10000;

    static constexpr const char* TAG = "Pn7160Reader";
};
