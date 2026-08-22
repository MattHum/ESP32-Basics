#include <Arduino.h>
#include "user_config.h"
#include "src/display/epd_driver.h"
#include "src/display/sci_ui.h"
#include "src/sensor/shtc3.h"
#include "src/battery/battery.h"

static EPDDriver epd;
static SHTC3 shtc3;

static volatile bool btnPressed = false;
static unsigned long lastUpdate = 0;
static unsigned long lastBtnTime = 0;

void IRAM_ATTR onButton() {
    unsigned long now = millis();
    if (now - lastBtnTime > 300) {
        btnPressed = true;
        lastBtnTime = now;
    }
}

void doUpdate() {
    SHTC3Data env = shtc3.read();
    BatteryData bat = battery_read();

    float temp = env.valid ? env.temperature : -999.0f;
    float humi = env.valid ? env.humidity : 0.0f;

    Serial.printf("[HUD] T=%.1fC  RH=%.1f%%  BAT=%.2fV %d%%\n",
                  temp, humi, bat.voltage, bat.percentage);

    sci_renderHUD(epd, temp, humi, bat.voltage, bat.percentage);
    epd.display();
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== TempHumPow - Env Monitor ===");

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << EPD_PWR_PIN) | (1ULL << VBAT_PWR_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_set_level((gpio_num_t)EPD_PWR_PIN, 0);
    gpio_set_level((gpio_num_t)VBAT_PWR_PIN, 1);
    delay(100);

    Serial.println("[INIT] EPD...");
    epd.init();
    epd.clear();
    epd.display();

    Serial.println("[INIT] SHTC3...");
    if (!shtc3.begin(Wire)) {
        Serial.println("[INIT] SHTC3 failed!");
    }

    Serial.println("[INIT] Battery ADC...");
    battery_init();

    pinMode(BTN_NEXT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BTN_NEXT_PIN), onButton, FALLING);

    Serial.println("[INIT] Ready. Press BOOT to refresh.\n");

    doUpdate();
    lastUpdate = millis();
}

void loop() {
    if (btnPressed) {
        btnPressed = false;
        Serial.println("[LOOP] Button pressed - updating...");
        doUpdate();
        lastUpdate = millis();
    }

    if (millis() - lastUpdate >= UPDATE_INTERVAL_MS) {
        Serial.println("[LOOP] Timer update...");
        doUpdate();
        lastUpdate = millis();
    }

    delay(50);
}
