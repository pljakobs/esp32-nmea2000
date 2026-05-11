#include "GwIcm20948Task.h"

#include <math.h>

#include <Adafruit_ICM20948.h>
#include <Adafruit_ICM20X.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <N2kMessages.h>
#include <Wire.h>

#include "GwHardware.h"

// imutype values
#define IMU_TYPE_OFF      0
#define IMU_TYPE_MPU6050  1
#define IMU_TYPE_ICM20948 2
// FreeRTOS stack depth is in words (4 bytes on ESP32), not bytes.
#define IMU_TASK_STACK_WORDS 6000

#ifndef GWIIC_SDA
#define GWIIC_SDA -1
#endif
#ifndef GWIIC_SCL
#define GWIIC_SCL -1
#endif

namespace {

static double normalize0To2Pi(double a) {
    while (a < 0) {
        a += 2.0 * M_PI;
    }
    while (a >= 2.0 * M_PI) {
        a -= 2.0 * M_PI;
    }
    return a;
}

static double clamp(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void runIcm20948Task(GwApi *api) {
    GwLog *logger = api->getLogger();
    GwConfigHandler *cfg = api->getConfig();

    const int imuType = cfg->getInt(GwConfigDefinitions::imutype, IMU_TYPE_OFF);
    const int i2cAddr = cfg->getInt(GwConfigDefinitions::icm20948addr, imuType == IMU_TYPE_ICM20948 ? 0x69 : 0x68);
    const int intervalMs = cfg->getInt(GwConfigDefinitions::icm20948intv, 100);
    const int iid = cfg->getInt(GwConfigDefinitions::icm20948iid, 60);
    const bool sendAttitude = cfg->getBool(GwConfigDefinitions::icm20948att, true);
    const bool sendRate = cfg->getBool(GwConfigDefinitions::icm20948rot, true);
    const bool sendHeading = cfg->getBool(GwConfigDefinitions::icm20948hdg, false);
    const bool useMagCfg = cfg->getBool(GwConfigDefinitions::icm20948usem, true);
    // MPU-6050 has no magnetometer
    const bool useMag = useMagCfg && (imuType == IMU_TYPE_ICM20948);
    const bool invertRoll = cfg->getBool(GwConfigDefinitions::icm20948rlinv, false);
    const bool invertPitch = cfg->getBool(GwConfigDefinitions::icm20948ptinv, false);
    float alphaCfg = 0.98f;
    float gyroZBiasCfg = 0.0f;
    float headingOffsetCfg = 0.0f;
    cfg->getValue(alphaCfg, GwConfigDefinitions::icm20948alpha, 0.98f);
    cfg->getValue(gyroZBiasCfg, GwConfigDefinitions::icm20948gzbs, 0.0f);
    cfg->getValue(headingOffsetCfg, GwConfigDefinitions::icm20948hdoff, 0.0f);
    const double alpha = clamp(alphaCfg, 0.0, 1.0);
    const double gyroZBias = (double)gyroZBiasCfg;
    const double headingOffset = (double)headingOffsetCfg * M_PI / 180.0;

    int sda = cfg->getInt(GwConfigDefinitions::icm20948sda, -1);
    int scl = cfg->getInt(GwConfigDefinitions::icm20948scl, -1);
    if (sda < 0) sda = GWIIC_SDA;
    if (scl < 0) scl = GWIIC_SCL;

    if (sda < 0 || scl < 0) {
        LOG_DEBUG(GwLog::ERROR, "IMU: invalid I2C pins sda=%d scl=%d", sda, scl);
        vTaskDelete(NULL);
        return;
    }

    if (!Wire.begin(sda, scl)) {
        LOG_DEBUG(GwLog::ERROR, "IMU: unable to init I2C on sda=%d scl=%d", sda, scl);
        vTaskDelete(NULL);
        return;
    }

    // Sensor objects — only one will be initialised at runtime
    Adafruit_MPU6050 mpu;
    Adafruit_ICM20948 icm;

    if (imuType == IMU_TYPE_MPU6050) {
        if (!mpu.begin((uint8_t)i2cAddr, &Wire)) {
            LOG_DEBUG(GwLog::ERROR, "MPU-6050: begin failed at address 0x%02x", i2cAddr);
            vTaskDelete(NULL);
            return;
        }
        LOG_DEBUG(GwLog::LOG, "MPU-6050 task started: addr=0x%02x intv=%dms iid=%d",
                  i2cAddr, intervalMs, iid);
    } else {
        if (!icm.begin_I2C((uint8_t)i2cAddr, &Wire, 0)) {
            LOG_DEBUG(GwLog::ERROR, "ICM-20948: begin failed at address 0x%02x", i2cAddr);
            vTaskDelete(NULL);
            return;
        }
        LOG_DEBUG(GwLog::LOG,
                  "ICM-20948 task started: addr=0x%02x intv=%dms iid=%d att=%d rot=%d hdg=%d mag=%d",
                  i2cAddr, intervalMs, iid,
                  (int)sendAttitude, (int)sendRate, (int)sendHeading, (int)useMag);
    }

    int counterId = api->addCounter("icm20948");
    uint8_t sid = 0;

    bool haveOrientation = false;
    double roll = 0;
    double pitch = 0;
    double yaw = 0;
    unsigned long lastMs = millis();

    while (true) {
        delay(intervalMs);

        sensors_event_t accel;
        sensors_event_t gyro;
        sensors_event_t temp;
        sensors_event_t mag;

        bool ok;
        if (imuType == IMU_TYPE_MPU6050) {
            ok = mpu.getEvent(&accel, &gyro, &temp);
        } else {
            ok = icm.getEvent(&accel, &gyro, &temp, &mag);
        }
        if (!ok) {
            api->increment(counterId, "readErr", true);
            continue;
        }

        unsigned long now = millis();
        double dt = (double)(now - lastMs) / 1000.0;
        if (dt <= 0 || dt > 1.0) {
            dt = (double)intervalMs / 1000.0;
        }
        lastMs = now;

        const double ax = accel.acceleration.x;
        const double ay = accel.acceleration.y;
        const double az = accel.acceleration.z;
        const double gz = gyro.gyro.z + gyroZBias;

        const double rollAcc = atan2(ay, az);
        const double pitchAcc = atan2(-ax, sqrt(ay * ay + az * az));

        if (!haveOrientation) {
            roll = rollAcc;
            pitch = pitchAcc;
            yaw = 0;
            haveOrientation = true;
        } else {
            roll = alpha * (roll + gyro.gyro.x * dt) + (1.0 - alpha) * rollAcc;
            pitch = alpha * (pitch + gyro.gyro.y * dt) + (1.0 - alpha) * pitchAcc;
            yaw = normalize0To2Pi(yaw + gz * dt);
        }

        if (invertRoll) {
            roll = -roll;
        }
        if (invertPitch) {
            pitch = -pitch;
        }

        if (useMag) {
            const double mx = mag.magnetic.x;
            const double my = mag.magnetic.y;
            const double mz = mag.magnetic.z;

            // Tilt-compensated heading in radians, normalized to [0, 2pi).
            const double cP = cos(pitch);
            const double sP = sin(pitch);
            const double cR = cos(roll);
            const double sR = sin(roll);
            const double mXh = mx * cP + mz * sP;
            const double mYh = mx * sR * sP + my * cR - mz * sR * cP;
            yaw = normalize0To2Pi(atan2(-mYh, mXh) + headingOffset);
        }

        if (sendAttitude) {
            tN2kMsg msg;
            SetN2kAttitude(msg, sid++, yaw, pitch, roll);
            api->sendN2kMessage(msg);
            api->increment(counterId, "127257");
        }

        if (sendRate) {
            tN2kMsg msg;
            SetN2kRateOfTurn(msg, sid++, gz);
            api->sendN2kMessage(msg);
            api->increment(counterId, "127251");
        }

        if (sendHeading && useMag) {
            tN2kMsg msg;
            SetN2kMagneticHeading(msg, sid++, yaw, N2kDoubleNA, N2kDoubleNA);
            api->sendN2kMessage(msg);
            api->increment(counterId, "127250");
        }
    }
}

} // namespace

void initIcm20948Task(GwApi *api) {
    const int imuType = api->getConfig()->getInt(GwConfigDefinitions::imutype, IMU_TYPE_OFF);
    if (imuType == IMU_TYPE_OFF) {
        return;
    }
    api->addCapability("icm20948", "true");
    if (imuType == IMU_TYPE_ICM20948) {
        api->addCapability("imuhasmag", "true");
    }
    api->addUserTask(runIcm20948Task, "icm20948Task", IMU_TASK_STACK_WORDS);
}
