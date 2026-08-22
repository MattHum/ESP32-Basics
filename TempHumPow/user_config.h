#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#include <Arduino.h>

#define EPD_PIN_CS     11
#define EPD_PIN_DC     10
#define EPD_PIN_RST    9
#define EPD_PIN_BUSY   8
#define EPD_PIN_MOSI   13
#define EPD_PIN_SCK    12
#define EPD_SPI_HOST   SPI2_HOST
#define EPD_BUFFER_LEN 10000

#define EPD_PWR_PIN    6
#define AUDIO_PWR_PIN  42
#define VBAT_PWR_PIN   17

#define I2C_SDA        47
#define I2C_SCL        48

#define BTN_NEXT_PIN   0
#define BTN_PREV_PIN   18

#define BATT_ADC_PIN   4
#define BATT_VOLTAGE_DIVIDER 2.0

#define EPD_WIDTH      200
#define EPD_HEIGHT     200

#define UPDATE_INTERVAL_MS 300000

#endif
