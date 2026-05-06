#pragma once

#include <array>
#include <cstdint>
#include <vector>

/**
 * @brief Abstract interface for NFC reader implementations.
 *
 * Provides a unified API over the PN532 (command/response) and PN7160 (NCI/event-driven)
 * protocols so that NfcManager can operate generically.
 */
class INfcReader {
public:
    virtual ~INfcReader() = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Initialize the reader hardware and protocol stack.
     * @return true on success, false otherwise.
     */
    virtual bool init() = 0;

    /**
     * @brief Shutdown the reader and release resources.
     */
    virtual void stop() = 0;

    /**
     * @brief Check if the reader is currently connected/responsive.
     */
    virtual bool isConnected() const = 0;

    // -------------------------------------------------------------------------
    // Version
    // -------------------------------------------------------------------------

    virtual uint8_t getFwMajor() const = 0;
    virtual uint8_t getFwMinor() const = 0;

    // -------------------------------------------------------------------------
    // Discovery / Polling
    // -------------------------------------------------------------------------

    /**
     * @brief Start RF discovery / polling for passive targets.
     * @return true on success.
     */
    virtual bool beginDiscovery() = 0;

    /**
     * @brief Wait for a passive target to enter the field.
     *
     * @param uid   Populated with the tag UID on success.
     * @param atqa  Populated with the 2-byte ATQA on success.
     * @param sak   Populated with the 1-byte SAK on success.
     * @param timeoutMs  Maximum time to wait for a tag.
     * @return true if a tag was detected, false on timeout or error.
     */
    virtual bool pollForTag(std::vector<uint8_t>& uid,
                            std::array<uint8_t, 2>& atqa,
                            uint8_t& sak,
                            uint32_t timeoutMs) = 0;

    /**
     * @brief Check if the previously detected tag is still in the RF field.
     */
    virtual bool isTagStillPresent() = 0;

    /**
     * @brief Release / deactivate the current tag.
     */
    virtual void releaseTag() = 0;

    /**
     * @brief Stop RF discovery.
     */
    virtual void endDiscovery() = 0;

    // -------------------------------------------------------------------------
    // Communication
    // -------------------------------------------------------------------------

    /**
     * @brief Send an ECP (Express Card Profile) wake-up frame.
     *
     * Only supported on readers that allow raw frame injection during discovery.
     * @return true if the frame was sent, false if unsupported or failed.
     */
    virtual bool sendEcp(const uint8_t* ecpData, size_t len) = 0;

    /**
     * @brief Exchange an APDU with the currently selected target.
     *
     * @param send    C-APDU bytes to transmit.
     * @param recv    R-APDU bytes received from the tag.
     * @param timeoutMs  Transaction timeout.
     * @return true on successful exchange.
     */
    virtual bool exchangeApdu(const std::vector<uint8_t>& send,
                              std::vector<uint8_t>& recv,
                              uint32_t timeoutMs) = 0;

    /**
     * @brief Perform a lightweight health check (e.g. register write/read).
     * @return true if the reader is responsive.
     */
    virtual bool healthCheck() = 0;
};
