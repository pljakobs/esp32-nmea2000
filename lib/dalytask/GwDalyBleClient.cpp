#include "GwDalyBleClient.h"

#include "GwHardware.h"

#include <BLEDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>
#include <BLEUtils.h>
#include <BLEClient.h>

GwDalyBleClient *GwDalyBleClient::activeClient_ = nullptr;

GwDalyBleClient::GwDalyBleClient(GwLog *logger)
    : logger_(logger),
      connected_(false),
      client_(nullptr),
      service_(nullptr),
      rxChar_(nullptr),
      txChar_(nullptr),
      responseLen_(0),
      responseReady_(false) {
}

GwDalyBleClient::~GwDalyBleClient() {
    disconnect();
}

bool GwDalyBleClient::connect(const std::string &macAddr) {
    if (connected_) {
        disconnect();
    }

    BLEDevice::init("");

    BLEAddress addr(macAddr);
    client_ = BLEDevice::createClient();

    if (client_ == nullptr) {
        LOG_DEBUG(GwLog::ERROR, "Daly: failed to create BLE client");
        return false;
    }

    if (!client_->connect(addr, BLE_ADDR_TYPE_RANDOM)) {
        LOG_DEBUG(GwLog::ERROR, "Daly: failed to connect to %s", macAddr.c_str());
        BLEDevice::deleteClient(client_);
        client_ = nullptr;
        return false;
    }

    service_ = client_->getService(BLEUUID((uint16_t)SERVICE_UUID));
    if (service_ == nullptr) {
        LOG_DEBUG(GwLog::ERROR, "Daly: service 0x%04x not found", SERVICE_UUID);
        disconnect();
        return false;
    }

    rxChar_ = service_->getCharacteristic(BLEUUID((uint16_t)RX_UUID));
    txChar_ = service_->getCharacteristic(BLEUUID((uint16_t)TX_UUID));
    if (rxChar_ == nullptr || txChar_ == nullptr) {
        LOG_DEBUG(GwLog::ERROR, "Daly: missing rx/tx characteristics");
        disconnect();
        return false;
    }

    if (rxChar_->canNotify()) {
        activeClient_ = this;
        rxChar_->registerForNotify(notifyCallback);
    }

    LOG_DEBUG(GwLog::LOG, "Daly: connected to %s", macAddr.c_str());
    connected_ = true;
    connectedMac_ = macAddr;

    return true;
}

void GwDalyBleClient::disconnect() {
    if (activeClient_ == this) {
        activeClient_ = nullptr;
    }

    if (client_ != nullptr) {
        if (client_->isConnected()) {
            client_->disconnect();
        }
        BLEDevice::deleteClient(client_);
        client_ = nullptr;
    }

    service_ = nullptr;
    rxChar_ = nullptr;
    txChar_ = nullptr;
    responseLen_ = 0;
    responseReady_ = false;
    connected_ = false;
    connectedMac_.clear();
}

bool GwDalyBleClient::isConnected() const {
    return connected_;
}

size_t GwDalyBleClient::sendRequest(const uint8_t *request, size_t requestLen,
                                     uint8_t *response, size_t responseMaxLen,
                                     uint32_t timeoutMs) {
    if (!connected_ || !client_ || !rxChar_ || !txChar_ || !request || !response || requestLen == 0) {
        return 0;
    }

    responseLen_ = 0;
    responseReady_ = false;

    if (!txChar_->writeValue((uint8_t *)request, requestLen, true)) {
        LOG_DEBUG(GwLog::WARN, "Daly: BLE write failed");
        return 0;
    }

    const unsigned long start = millis();
    while ((millis() - start) < timeoutMs) {
        if (responseReady_) {
            const size_t copyLen = (responseLen_ < responseMaxLen) ? responseLen_ : responseMaxLen;
            if (copyLen > 0) {
                memcpy(response, responseBuf_, copyLen);
            }
            return copyLen;
        }

        // Some Daly implementations expose RX as readable without notify.
        if (rxChar_->canRead()) {
            std::string rv = rxChar_->readValue();
            if (!rv.empty()) {
                const size_t copyLen = (rv.size() < responseMaxLen) ? rv.size() : responseMaxLen;
                memcpy(response, rv.data(), copyLen);
                return copyLen;
            }
        }

        delay(20);
    }

    return 0;
}

void GwDalyBleClient::notifyCallback(BLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify) {
    (void)chr;
    (void)isNotify;
    if (activeClient_ != nullptr) {
        activeClient_->handleNotify(data, len);
    }
}

void GwDalyBleClient::handleNotify(const uint8_t *data, size_t len) {
    if (data == nullptr || len == 0) {
        return;
    }
    const size_t copyLen = (len < sizeof(responseBuf_)) ? len : sizeof(responseBuf_);
    memcpy(responseBuf_, data, copyLen);
    responseLen_ = copyLen;
    responseReady_ = true;
}
