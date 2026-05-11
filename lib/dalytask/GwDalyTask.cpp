#include "GwDalyTask.h"

#include "GWConfig.h"

namespace {

static constexpr const char *CFG_ENABLED = "dalyEnabled";
static constexpr const char *CFG_NAME = "dalyName";
static constexpr const char *CFG_MAC = "dalyMac";
static constexpr const char *CFG_INTERVAL = "dalyPollIntv";
static constexpr const char *CFG_INSTANCE = "dalyInst";
static constexpr const char *CFG_DEBUG = "dalyDebug";
static constexpr int DALY_TASK_STACK = 4096;

void runDalyTask(GwApi *api) {
    GwConfigHandler *cfg = api->getConfig();
    GwLog *logger = api->getLogger();

    const String batteryName = cfg->getString(CFG_NAME);
    const String batteryMac = cfg->getString(CFG_MAC);
    const int intervalMs = cfg->getInt(CFG_INTERVAL, 5000);
    const int instance = cfg->getInt(CFG_INSTANCE, 0);
    const bool debug = cfg->getBool(CFG_DEBUG, false);

    const int counterId = api->addCounter("daly");

    LOG_DEBUG(GwLog::LOG,
              "daly task started: name=%s mac=%s inst=%d intv=%dms",
              batteryName.c_str(),
              batteryMac.c_str(),
              instance,
              intervalMs);

    while (true) {
        delay(intervalMs);
        api->increment(counterId, "loop");
        if (debug) {
            logger->logDebug(GwLog::DEBUG,
                             "daly placeholder loop: name=%s mac=%s inst=%d",
                             batteryName.c_str(),
                             batteryMac.c_str(),
                             instance);
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