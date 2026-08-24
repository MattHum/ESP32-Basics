#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#include <Arduino.h>

/* ── Display: 1.54G four-color e-paper ─────────────────────────── */
#define EPD_PIN_CS     11
#define EPD_PIN_DC     10
#define EPD_PIN_RST    9
#define EPD_PIN_BUSY   8
#define EPD_PIN_MOSI   13
#define EPD_PIN_SCK    12
#define EPD_SPI_HOST   SPI2_HOST
#define EPD_BUFFER_LEN 10000          /* 200×200 / 4 = 10000 bytes */

/* ── Power ─────────────────────────────────────────────────────── */
#define EPD_PWR_PIN    6               /* LOW = EPD power ON  */
#define AUDIO_PWR_PIN  42              /* LOW = audio amp ON  */
#define VBAT_PWR_PIN   17              /* HIGH = battery ON   */

/* ── Audio: I2S + ES8311 ──────────────────────────────────────── */
#define I2S_MCK_PIN    14
#define I2S_BCK_PIN    15
#define I2S_LRCK_PIN   38
#define I2S_DOUT_PIN   45
#define I2S_DIN_PIN    16
#define PA_CTRL_PIN    46
#define PA_EN_PIN      42

#define I2C_SDA        47
#define I2C_SCL        48

#define SAMPLE_RATE    24000
#define MCLK_MULTIPLE  256
#define MCLK_FREQ      (SAMPLE_RATE * MCLK_MULTIPLE)
#define VOICE_VOLUME   70

/* ── Buttons ───────────────────────────────────────────────────── */
#define BTN_NEXT_PIN   0               /* BOOT button – next image + play */
#define BTN_PREV_PIN   18              /* PWR button  – prev image (optional) */

/* ── SD Card (SPI mode) ───────────────────────────────────────── */
#define SD_CS_PIN      -1              /* set if SPI SD used; -1 = not used */
/* On 1.54G the SD slot is native SPI via HSPI, using default pins unless overridden */

/* ── Display geometry ──────────────────────────────────────────── */
#define EPD_WIDTH      200
#define EPD_HEIGHT     200

#endif
