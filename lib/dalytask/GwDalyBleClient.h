#ifndef _GWDALYBLECLIENT_H
#define _GWDALYBLECLIENT_H

#include <cstdint>
#include <string>
#include <cstring>

class GwLog;
class BLEClient;
class BLERemoteService;
class BLERemoteCharacteristic;

/**
 * Minimal BLE client for Daly BMS communication.
 * Handles connection, discovery, and request/response cycles.
 */
class GwDalyBleClient {
public:
    GwDalyBleClient(GwLog *logger);
    ~GwDalyBleClient();

    /**
     * Connect to a Daly BMS by MAC address.
     * @param macAddr MAC address as string (e.g., "AA:BB:CC:DD:EE:FF")
     * @return true if connect succeeded
     */
    bool connect(const std::string &macAddr);

    /**
     * Disconnect from the BMS.
     */
    void disconnect();

    /**
     * Check if currently connected.
     * @return true if connected and ready
     */
    bool isConnected() const;

    /**
     * Send a request and read response (blocking).
     * @param request buffer with request frame
     * @param requestLen length of request
     * @param response buffer for response (must be at least 64 bytes)
     * @param responseMaxLen max response size
     * @param timeoutMs read timeout in milliseconds
     * @return number of bytes read, or 0 on timeout/error
     */
    size_t sendRequest(const uint8_t *request, size_t requestLen,
                       uint8_t *response, size_t responseMaxLen,
                       uint32_t timeoutMs = 5000);

private:
    static void notifyCallback(BLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify);
    void handleNotify(const uint8_t *data, size_t len);

    GwLog *logger_;
    bool connected_;
    std::string connectedMac_;
    BLEClient *client_;
    BLERemoteService *service_;
    BLERemoteCharacteristic *rxChar_;
    BLERemoteCharacteristic *txChar_;
    uint8_t responseBuf_[256];
    size_t responseLen_;
    volatile bool responseReady_;
    static GwDalyBleClient *activeClient_;

    static constexpr uint16_t SERVICE_UUID = 0xFFF0;
    static constexpr uint16_t RX_UUID = 0xFFF1;
    static constexpr uint16_t TX_UUID = 0xFFF2;
};

#endif
