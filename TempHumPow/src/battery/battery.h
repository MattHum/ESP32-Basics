#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

struct BatteryData {
    float voltage;
    int percentage;
};

void battery_init();
BatteryData battery_read();

#endif
