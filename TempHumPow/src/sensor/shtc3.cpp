#include "shtc3.h"
#include "user_config.h"
#include <Arduino.h>

static const uint16_t CMD_WAKEUP    = 0x3517;
static const uint16_t CMD_SLEEP     = 0xB098;
static const uint16_t CMD_SOFTRESET = 0x805D;
static const uint16_t CMD_MEAS_T_RH = 0x7866;
static const uint16_t CMD_READ_ID   = 0xEFC8;
static const uint8_t  CRC_POLY      = 0x31;

bool SHTC3::begin(TwoWire &wire) {
    _wire = &wire;
    _wire->begin(I2C_SDA, I2C_SCL, 400000);
    delay(20);
    if (!softReset()) return false;
    delay(20);
    return wake();
}

bool SHTC3::isConnected() {
    uint8_t cmd[2] = {(uint8_t)(CMD_READ_ID >> 8), (uint8_t)(CMD_READ_ID & 0xFF)};
    _wire->beginTransmission(SHTC3_ADDR);
    _wire->write(cmd, 2);
    if (_wire->endTransmission() != 0) return false;
    if (_wire->requestFrom((uint8_t)SHTC3_ADDR, (uint8_t)3) != 3) return false;
    uint8_t buf[3];
    buf[0] = _wire->read();
    buf[1] = _wire->read();
    buf[2] = _wire->read();
    return calcCRC(buf, 2) == buf[2];
}

bool SHTC3::wake() {
    uint8_t cmd[2] = {(uint8_t)(CMD_WAKEUP >> 8), (uint8_t)(CMD_WAKEUP & 0xFF)};
    _wire->beginTransmission(SHTC3_ADDR);
    _wire->write(cmd, 2);
    delay(1);
    return _wire->endTransmission() == 0;
}

bool SHTC3::sleep() {
    uint8_t cmd[2] = {(uint8_t)(CMD_SLEEP >> 8), (uint8_t)(CMD_SLEEP & 0xFF)};
    _wire->beginTransmission(SHTC3_ADDR);
    _wire->write(cmd, 2);
    return _wire->endTransmission() == 0;
}

bool SHTC3::softReset() {
    uint8_t cmd[2] = {(uint8_t)(CMD_SOFTRESET >> 8), (uint8_t)(CMD_SOFTRESET & 0xFF)};
    _wire->beginTransmission(SHTC3_ADDR);
    _wire->write(cmd, 2);
    return _wire->endTransmission() == 0;
}

uint8_t SHTC3::calcCRC(uint8_t data[], uint8_t len) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; bit--) {
            if (crc & 0x80)
                crc = (crc << 1) ^ CRC_POLY;
            else
                crc = (crc << 1);
        }
    }
    return crc;
}

SHTC3Data SHTC3::read() {
    SHTC3Data result = {0, 0, false};

    if (!wake()) return result;
    delay(1);

    uint8_t cmd[2] = {(uint8_t)(CMD_MEAS_T_RH >> 8), (uint8_t)(CMD_MEAS_T_RH & 0xFF)};
    _wire->beginTransmission(SHTC3_ADDR);
    _wire->write(cmd, 2);
    if (_wire->endTransmission() != 0) {
        sleep();
        return result;
    }

    delay(25);

    if (_wire->requestFrom((uint8_t)SHTC3_ADDR, (uint8_t)6) != 6) {
        sleep();
        return result;
    }

    uint8_t buf[6];
    for (int i = 0; i < 6; i++) buf[i] = _wire->read();

    if (calcCRC(buf, 2) != buf[2]) {
        sleep();
        return result;
    }
    if (calcCRC(buf + 3, 2) != buf[5]) {
        sleep();
        return result;
    }

    uint16_t rawT = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t rawH = ((uint16_t)buf[3] << 8) | buf[4];

    result.temperature = 175.0f * (float)rawT / 65536.0f - 45.0f;
    result.humidity = 100.0f * (float)rawH / 65536.0f;
    result.valid = true;

    sleep();
    return result;
}
