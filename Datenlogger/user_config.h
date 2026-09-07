#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#include <Arduino.h>

/* ── Display: 1.54" G (4-Farben) e-paper ───────────────────────── */
#define EPD_PIN_CS     11
#define EPD_PIN_DC     10
#define EPD_PIN_RST    9
#define EPD_PIN_BUSY   8
#define EPD_PIN_MOSI   13
#define EPD_PIN_SCK    12
#define EPD_SPI_HOST   SPI2_HOST
#define EPD_BUFFER_LEN 10000          /* 200×200 / 4 = 10000 Bytes (2 bit/Pixel) */
#define EPD_WIDTH      200
#define EPD_HEIGHT     200

/* ── Power ─────────────────────────────────────────────────────── */
#define EPD_PWR_PIN    6              /* LOW = EPD-Versorgung AN  (aus eurem board_power_bsp!) */
#define AUDIO_PWR_PIN  42             /* LOW = Audio-Rail AN. MUSS an sein, sonst blockiert der ES8311 den I2C-Bus! */
#define VBAT_PWR_PIN   17             /* HIGH = Akku-Versorgung AN, im Deep Sleep halten */

/* ── I2C (geteilter Bus: SHTC3 0x70, PCF85063 0x51, ES8311 0x18) ─ */
#define I2C_SDA        47
#define I2C_SCL        48

/* ── Sensor / RTC / Batterie ───────────────────────────────────── */
#define SHTC3_ADDR       0x70
#define RTC_I2C_ADDR     0x51         /* PCF85063ATL: Zeitregister ab 0x04! */
#define BATTERY_ADC_PIN  4            /* Teiler-Faktor 2 */

/* ── SD-Karte: SD_MMC 1-Bit (KEIN CS!)  -> setPins(clk,cmd,d0) ──── */
#define SD_CLK_PIN     39
#define SD_CMD_PIN     41             /* = SD_MOSI-Netz */
#define SD_D0_PIN      40             /* = SD_MISO-Netz */

/* ── Taster ────────────────────────────────────────────────────── */
#define BTN_PWR_PIN    18             /* PWR-Taster, aktiv LOW, ext1-Weckquelle */

#endif
