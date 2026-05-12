#include "GwDalyTask.h"

#include "GWConfig.h"
#include "GwDalyBleClient.h"
#include "GwDalyProtocol.h"
#include <N2kMessages.h>
#include <math.h>
#include <algorithm>

namespace {

static constexpr const char *CFG_ENABLED = "dalyEnabled";
static constexpr const char *CFG_NAME = "dalyName";
static constexpr const char *CFG_MAC = "dalyMac";
static constexpr const char *CFG_INTERVAL = "dalyPollIntv";
static constexpr const char *CFG_INSTANCE = "dalyInst";
static constexpr const char *CFG_DEBUG = "dalyDebug";
static constexpr const char *CFG_SOC_LOG = "dalySocLog";
static constexpr int DALY_TASK_STACK = 6000;
static constexpr uint32_t BLE_READ_TIMEOUT = 5000;  // 5 second timeout for BLE reads
static constexpr uint32_t RECONNECT_BASE_DELAY_MS = 1000;

void runDalyTask(GwApi *api) {
    GwConfigHandler *cfg = api->getConfig();
    GwLog *logger = api->getLogger();

    const String batteryName = cfg->getString(CFG_NAME);
    const String batteryMac = cfg->getString(CFG_MAC);
    const int intervalMs = cfg->getInt(CFG_INTERVAL, 60000);  // Default 1 minute
    const int instance = cfg->getInt(CFG_INSTANCE, 0);
    const bool debug = cfg->getBool(CFG_DEBUG, false);
    const bool socLog = cfg->getBool(CFG_SOC_LOG, true);

    const int counterId = api->addCounter("daly");

    LOG_DEBUG(GwLog::LOG,
              "daly task started: name=%s mac=%s inst=%d intv=%dms debug=%d",
              batteryName.c_str(),
              batteryMac.c_str(),
              instance,
              intervalMs,
              (int)debug);

    // Initialize BLE client
    GwDalyBleClient bleClient(logger);
    uint32_t reconnectDelay = RECONNECT_BASE_DELAY_MS;
    unsigned long lastPollTime = 0;
    uint8_t sid = 0;

    while (true) {
        unsigned long now = millis();

        // Try to connect if not already connected
        if (!bleClient.isConnected()) {
            LOG_DEBUG(GwLog::LOG, "daly: attempting BLE connect to %s", batteryMac.c_str());
            if (bleClient.connect(batteryMac.c_str())) {
                api->increment(counterId, "connectOk");
                reconnectDelay = RECONNECT_BASE_DELAY_MS;  // Reset backoff on successful connect
                lastPollTime = now;
            } else {
                api->increment(counterId, "connectFail");
                LOG_DEBUG(GwLog::WARN, "daly: BLE connect failed, retry in %dms", reconnectDelay);
                delay(reconnectDelay);
                // Exponential backoff with cap
                reconnectDelay = std::min((uint32_t)(reconnectDelay * 1.5f), (uint32_t)300000);
                continue;
            }
        }

        // Check if it's time to poll
        if (now - lastPollTime < (unsigned long)intervalMs) {
            delay(100);  // Small delay to avoid busy spinning
            continue;
        }

        lastPollTime = now;

        // Build request frame for pack data
        uint8_t requestBuf[16];
        size_t requestLen = GwDalyProtocol::buildPackDataRequest(requestBuf, sizeof(requestBuf));

        if (requestLen == 0) {
            api->increment(counterId, "buildReqFail");
            LOG_DEBUG(GwLog::ERROR, "daly: failed to build request frame");
            continue;
        }

        if (debug) {
            LOG_DEBUG(GwLog::DEBUG, "daly: sending request (%zu bytes)", requestLen);
        }

        // Send request and get response
        uint8_t responseBuf[64];
        size_t responseLen = bleClient.sendRequest(requestBuf, requestLen,
                                                    responseBuf, sizeof(responseBuf),
                                                    BLE_READ_TIMEOUT);

        if (responseLen == 0) {
            api->increment(counterId, "pollTimeout");
            LOG_DEBUG(GwLog::WARN, "daly: poll timeout or BLE error");
            bleClient.disconnect();  // Force reconnect on next iteration
            continue;
        }

        if (debug) {
            LOG_DEBUG(GwLog::DEBUG, "daly: received response (%zu bytes)", responseLen);
        }

        // Decode response
        DalyPackData packData;
        if (!GwDalyProtocol::decodePackDataResponse(responseBuf, responseLen, packData)) {
            api->increment(counterId, "decodeFail");
            LOG_DEBUG(GwLog::WARN, "daly: failed to decode response");
            continue;
        }

        api->increment(counterId, "pollOk");

        if (debug) {
            LOG_DEBUG(GwLog::DEBUG,
                      "daly: V=%.2fV I=%.2fA T=%.1fC SoC=%.0f%%",
                      packData.voltage_v, packData.current_a,
                      packData.temperature_c, packData.soc_pct);
        }

        // Send PGN 127508: Battery Status with voltage, current, temperature
        // SetN2kBatteryStatus requires voltage (V), current (A), temperature (K), instance, SID
        tN2kMsg msg;
        double voltageV = packData.voltage_v;
        double currentA = packData.current_a;
        double tempK = std::isfinite(packData.temperature_c) ? (packData.temperature_c + 273.15) : N2kDoubleNA;

        // Use N2kDoubleNA for unavailable values
        SetN2kBatteryStatus(msg,
                           instance,            // battery instance
                           voltageV,            // voltage in V
                           currentA,            // current in A (positive discharge)
                           tempK,               // temperature in K
                           sid++);              // sequence ID

        api->sendN2kMessage(msg);
        api->increment(counterId, "127508");

        // SoC is decoded and reported in logs for monitoring/analytics.
        if ((debug || socLog) && std::isfinite(packData.soc_pct) && packData.soc_pct >= 0 && packData.soc_pct <= 100) {
            LOG_DEBUG(debug ? GwLog::DEBUG : GwLog::LOG, "daly: SoC = %.1f%%", packData.soc_pct);
        }
    }
}

} // namespace

void initDalyTask(GwApi *api) {
    GwConfigHandler *cfg = api->getConfig();
    if (!cfg->getBool(CFG_ENABLED, false)) {
        return;
    }

    api->addCapability("daly", "true");
    api->addUserTask(runDalyTask, "dalyTask", DALY_TASK_STACK);
}