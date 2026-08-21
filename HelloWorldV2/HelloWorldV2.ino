#include <Arduino.h>
#include "user_config.h"
#include "src/display/epaper_driver_bsp.h"
#include "src/power/board_power_bsp.h"
#include "src/audio/audio_player.h"
#include "src/sd/sdcard_bsp.h"
#include "images/images.h"

/* ── Hardware ─────────────────────────────────────────────────── */
static board_power_bsp_t power(EPD_PWR_PIN, AUDIO_PWR_PIN, VBAT_PWR_PIN);
static epaper_driver_display *epd = nullptr;
static AudioPlayer audio;

/* ── State ────────────────────────────────────────────────────── */
static volatile int current_index = 0;
static volatile bool btn_pressed = false;
static unsigned long last_btn_time = 0;
static bool audio_ready = false;

/* ── Button ISR ───────────────────────────────────────────────── */
void IRAM_ATTR onButtonNext() {
    unsigned long now = millis();
    if (now - last_btn_time > 300) {
        btn_pressed = true;
        last_btn_time = now;
    }
}

/* ── Display image ────────────────────────────────────────────── */
void displayImage(int index) {
    if (index < 0 || index >= IMAGE_COUNT) return;
    Serial.printf("[APP] Bild %d anzeigen\n", index);

    const unsigned char *img = images[index];
    memcpy(epd->getBuffer(), img, EPD_BUFFER_LEN);
    epd->EPD_Display();
}

/* ── Play audio if available ──────────────────────────────────── */
void playAudioIfExists(int index) {
    const char *path = audio_files[index];
    if (!path) {
        Serial.printf("[APP] Bild %d – kein Ton\n", index);
        return;
    }
    if (!audio_ready) {
        Serial.printf("[APP] Audio nicht bereit, überspringe %s\n", path);
        return;
    }
    if (!sdcard_fileExists(path)) {
        Serial.printf("[APP] %s nicht auf SD-Karte\n", path);
        return;
    }
    Serial.printf("[APP] Spiele Ton: %s\n", path);
    audio.playFile(path);
}

/* ── Setup ────────────────────────────────────────────────────── */
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== HelloWorldV2 – Dose → Kopf + Ton → Mücke + Ton ===");

    /* Power ON */
    power.POWEER_EPD_ON();
    delay(100);

    /* Display init */
    custom_lcd_spi_t spi_cfg = {
        .cs   = EPD_PIN_CS,
        .dc   = EPD_PIN_DC,
        .rst  = EPD_PIN_RST,
        .busy = EPD_PIN_BUSY,
        .mosi = EPD_PIN_MOSI,
        .scl  = EPD_PIN_SCK,
        .spi_host = EPD_SPI_HOST,
        .buffer_len = EPD_BUFFER_LEN
    };
    epd = new epaper_driver_display(EPD_WIDTH, EPD_HEIGHT, spi_cfg);

    Serial.println("[SETUP] EPD Init...");
    epd->EPD_Init();

    /* Clear white */
    epd->EPD_Clear();
    epd->EPD_Display();

    /* Show first image: Dose */
    displayImage(current_index);

    /* Audio init */
    power.POWEER_Audio_ON();
    delay(100);
    audio_ready = audio.begin();
    if (!audio_ready) {
        Serial.println("[SETUP] Audio-Init fehlgeschlagen – nur Bildwechsel");
    }

    /* SD card */
    sdcard_init();

    /* Button: BOOT (GPIO0) */
    pinMode(BTN_NEXT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BTN_NEXT_PIN), onButtonNext, FALLING);

    Serial.println("[SETUP] Bereit. BOOT-Taste drücken für nächste Szene.\n");
}

/* ── Loop ─────────────────────────────────────────────────────── */
void loop() {
    if (btn_pressed) {
        btn_pressed = false;

        /* Nächstes Bild */
        current_index = (current_index + 1) % IMAGE_COUNT;
        displayImage(current_index);

        /* Ton abspielen (nur bei Kopf und Mücke) */
        playAudioIfExists(current_index);
    }

    audio.loop();
    delay(10);
}
