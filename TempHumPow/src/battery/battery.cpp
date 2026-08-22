#include "battery.h"
#include "user_config.h"
#include <Arduino.h>

static const float VOLTAGE_MIN = 3.2f;
static const float VOLTAGE_MAX = 4.2f;

void battery_init() {
    analogReadResolution(12);
    pinMode(BATT_ADC_PIN, INPUT);
}

BatteryData battery_read() {
    BatteryData result = {0.0f, 0};

    uint32_t pinMv = analogReadMilliVolts(BATT_ADC_PIN);
    result.voltage = (float)pinMv * 0.001f * BATT_VOLTAGE_DIVIDER;

    int pct = (int)((result.voltage - VOLTAGE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    result.percentage = pct;

    return result;
}
