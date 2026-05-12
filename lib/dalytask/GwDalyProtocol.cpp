#include "GwDalyProtocol.h"

#include <cmath>

uint16_t GwDalyProtocol::computeCrcModbus(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x0001) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

size_t GwDalyProtocol::buildPackDataRequest(uint8_t *buf, size_t bufsize) {
    if (buf == nullptr || bufsize < REQUEST_LEN) {
        return 0;
    }

    // aiobmsble _cmd_modbus(dev_id=0xD2, fct=0x03, addr=0x0000, count=62)
    buf[0] = DEV_ID;
    buf[1] = FCT_READ;
    buf[2] = (uint8_t)(REG_ADDR_INFO >> 8);
    buf[3] = (uint8_t)(REG_ADDR_INFO & 0xFF);
    buf[4] = (uint8_t)(REG_COUNT_INFO >> 8);
    buf[5] = (uint8_t)(REG_COUNT_INFO & 0xFF);
    const uint16_t crc = computeCrcModbus(buf, 6);
    buf[6] = (uint8_t)(crc & 0xFF);        // little-endian
    buf[7] = (uint8_t)((crc >> 8) & 0xFF); // little-endian
    return REQUEST_LEN;
}

bool GwDalyProtocol::validateChecksum(const uint8_t *buf, size_t len) {
    if (buf == nullptr || len < MIN_RESPONSE_LEN) {
        return false;
    }

    if (buf[0] != DEV_ID || buf[1] != FCT_READ) {
        return false;
    }

    const uint8_t byteCount = buf[2];
    if (len != (size_t)byteCount + 5) {
        return false;
    }

    const uint16_t expected = (uint16_t)buf[len - 2] | ((uint16_t)buf[len - 1] << 8);
    const uint16_t computed = computeCrcModbus(buf, len - 2);
    return expected == computed;
}

bool GwDalyProtocol::decodePackDataResponse(const uint8_t *buf, size_t len, DalyPackData &out) {
    out.valid = false;
    out.voltage_v = NAN;
    out.current_a = NAN;
    out.soc_pct = NAN;
    out.temperature_c = NAN;

    if (buf == nullptr || len < MIN_RESPONSE_LEN) {
        return false;
    }

    if (!validateChecksum(buf, len)) {
        return false;
    }

    const uint8_t byteCount = buf[2];
    const uint8_t *payload = &buf[3];

    // aiobmsble field mapping for addr 0x0000,count=62 payload:
    // voltage at pos 80 (2 bytes, /10), current at pos 82 (2 bytes, (raw-30000)/10),
    // SoC at pos 84 (2 bytes, /10), temp_sensors at pos 100 (2 bytes),
    // temp values from pos 64 (2 bytes each, signed, offset 40).
    if (byteCount < 86) {
        return false;
    }

    const uint16_t voltageRaw = ((uint16_t)payload[80] << 8) | payload[81];
    const uint16_t currentRaw = ((uint16_t)payload[82] << 8) | payload[83];
    const uint16_t socRaw = ((uint16_t)payload[84] << 8) | payload[85];

    out.voltage_v = (float)voltageRaw / 10.0f;
    out.current_a = ((float)currentRaw - 30000.0f) / 10.0f;
    out.soc_pct = (float)socRaw / 10.0f;

    if (byteCount >= 102) {
        const uint16_t tempSensorCount = ((uint16_t)payload[100] << 8) | payload[101];
        if (tempSensorCount > 0 && byteCount >= 66) {
            const int16_t tempRaw = (int16_t)(((uint16_t)payload[64] << 8) | payload[65]);
            out.temperature_c = (float)tempRaw - 40.0f;
        }
    }

    out.valid = true;
    return true;
}
