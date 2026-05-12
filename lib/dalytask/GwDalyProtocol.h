#ifndef _GWDALYPROTOCOL_H
#define _GWDALYPROTOCOL_H

#include <cstdint>
#include <cstddef>

/**
 * Daly BMS protocol support.
 * Minimal implementation for reading pack voltage, current, SoC, and temperature.
 */

struct DalyPackData {
    float voltage_v;        // pack voltage in volts
    float current_a;        // pack current in amps (positive = discharge)
    float soc_pct;          // state of charge in percent
    float temperature_c;    // temperature in celsius
    bool valid;             // true if data was successfully decoded
};

class GwDalyProtocol {
public:
    /**
    * Build Daly Modbus request to read pack data.
    * Command mirrors aiobmsble: dev_id=0xD2, fct=0x03, addr=0x0000, count=62.
    * @param buf output buffer (must be at least 8 bytes)
     * @param bufsize size of output buffer
     * @return number of bytes written, or 0 if error
     */
    static size_t buildPackDataRequest(uint8_t *buf, size_t bufsize);

    /**
     * Decode a Daly response frame.
     * @param buf input buffer
     * @param len length of input buffer
     * @param out output structure
     * @return true if decode succeeded
     */
    static bool decodePackDataResponse(const uint8_t *buf, size_t len, DalyPackData &out);

    /**
     * Validate frame checksum.
     * @param buf buffer with frame
     * @param len length of frame
     * @return true if checksum is valid
     */
    static bool validateChecksum(const uint8_t *buf, size_t len);

private:
    static constexpr uint8_t DEV_ID = 0xD2;
    static constexpr uint8_t FCT_READ = 0x03;
    static constexpr uint16_t REG_ADDR_INFO = 0x0000;
    static constexpr uint16_t REG_COUNT_INFO = 62;
    static constexpr size_t REQUEST_LEN = 8;
    static constexpr size_t MIN_RESPONSE_LEN = 5; // dev + fct + bytecount + crc16

    // CRC-16/MODBUS (poly 0xA001), little-endian in frame.
    static uint16_t computeCrcModbus(const uint8_t *data, size_t len);
};

#endif
