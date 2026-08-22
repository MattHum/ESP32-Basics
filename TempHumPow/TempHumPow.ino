/*
  ============================================================================
  TempHumPow — SciFi-HUD Anzeige für Waveshare ESP32-S3-ePaper-1.54G
  ============================================================================
  Zeigt an:
    - Innentemperatur + Feuchtigkeit (onboard SHTC3)
    - Wien Außentemperatur + Regenwahrscheinlichkeit (Open-Meteo API)
    - Akkustand (ADC-Batteriemessung)
  Update: alle 5 Minuten (Deep Sleep Timer) ODER sofort per PWR-Taster.

  WICHTIGER HINWEIS ZUR HARDWARE-KOMPATIBILITÄT:
  Dieser Sketch verwendet eine GxEPD2-ähnliche Display-API (drawPixel,
  fillRect, drawCircle, setCursor, print ...) für die HUD-Zeichnung.
  Das 1.54G-Panel ist ein 4-Farb-Panel (Schwarz/Weiß/Rot/Gelb) mit einem
  eigenen SPI-Controller. Falls GxEPD2 in deiner installierten Version
  (noch) keine passende Klasse für dieses Panel mitbringt:
    -> Nutze stattdessen den Display-Treiber aus Waveshares eigenem
       GitHub-Repo (waveshareteam/ESP32-S3-ePaper-1.54G, Ordner
       Example/Arduino_3.2.0). Ersetze NUR die Display-Init- und
       Display-Refresh-Aufrufe unten (Abschnitt "DISPLAY TREIBER").
       Die restliche Logik (WLAN, Sensoren, API, Power-Management,
       Deep Sleep) bleibt unverändert gültig.

  Ebenso: Der exakte GPIO für die Batteriespannungsmessung ist in
  Waveshares Arduino_3.2.0-Beispiel enthalten ("ADC battery measurement").
  Trage den dort verwendeten Pin unten bei BATTERY_ADC_PIN ein und
  kalibriere den Spannungsteiler-Faktor (VOLTAGE_DIVIDER_RATIO) anhand
  einer Messung mit Multimeter.
  ============================================================================
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <time.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"

// ---------------------------------------------------------------------------
// BOARD-SPEZIFISCHES POWER-MANAGEMENT (ESP32-S3-ePaper-1.54G)
// ---------------------------------------------------------------------------
#define PIN_PWR_BUTTON_SENSE   18   // liest PWR-Tasterzustand
#define PIN_VBAT_LATCH         17   // MUSS beim Boot sofort HIGH -> hält Batteriestrom
#define PIN_EPD_POWER           6   // LOW = Panel bekommt Strom, HIGH = Panel aus

// I2C (SHTC3 + PCF85063 RTC)
#define PIN_I2C_SDA            47
#define PIN_I2C_SCL            48

// TODO: exakten Pin aus Waveshares Arduino_3.2.0 ADC-Batterie-Beispiel eintragen
#define BATTERY_ADC_PIN         -1   // Platzhalter -> unbedingt anpassen!
#define VOLTAGE_DIVIDER_RATIO   2.0f // Platzhalter -> mit Multimeter kalibrieren
#define BATTERY_MIN_V           3.3f
#define BATTERY_MAX_V           4.2f

// Deep-Sleep-Intervall
#define SLEEP_SECONDS      (5 * 60)   // 5 Minuten
// Siehe Hinweis unten zu Akkulaufzeit bei diesem Intervall!

// Wien-Koordinaten für Open-Meteo
#define VIENNA_LAT  "48.2082"
#define VIENNA_LON  "16.3738"

// ---------------------------------------------------------------------------
// GLOBALE DATENSTRUKTUR
// ---------------------------------------------------------------------------
struct HudData {
  float tempIn   = NAN;
  float humIn    = NAN;
  float tempOut  = NAN;
  int   rainOut  = -1;
  int   batteryPct = -1;
  bool  wifiOk   = false;
  char  timeStr[6] = "--:--";
};

HudData data;

// ---------------------------------------------------------------------------
// DISPLAY TREIBER (Platzhalter — siehe Hinweis oben)
// ---------------------------------------------------------------------------
// #include <GxEPD2_4C.h>
// GxEPD2_4C<...> display(...); // Panel-spezifische Konstruktor-Parameter
//
// Für dieses Beispiel nehmen wir an, "display" bietet Adafruit_GFX-kompatible
// Methoden (fillScreen, drawPixel, drawLine, drawRect, drawCircle,
// setCursor, setTextSize, print, display()).
//
// Falls du Waveshares eigenen Treiber nutzt, kapsle ihn in ein Objekt mit
// derselben Methodensignatur, damit drawHud() unten unverändert bleibt.

void initDisplay() {
  // TODO: Panel-Init aus Waveshare-Beispiel oder GxEPD2 hier aufrufen
  // display.init();
}

void pushDisplay() {
  // TODO: kompletten Bildschirmaufbau ans Panel senden (full refresh)
  // display.display();
}

// ---------------------------------------------------------------------------
// SETUP-HILFSFUNKTIONEN
// ---------------------------------------------------------------------------

void latchBatteryPower() {
  // MUSS als Allererstes passieren, sonst stirbt das Board im
  // Batteriebetrieb sofort nach Loslassen des PWR-Tasters.
  pinMode(PIN_VBAT_LATCH, OUTPUT);
  digitalWrite(PIN_VBAT_LATCH, HIGH);

  pinMode(PIN_EPD_POWER, OUTPUT);
  digitalWrite(PIN_EPD_POWER, LOW); // Panel einschalten

  pinMode(PIN_PWR_BUTTON_SENSE, INPUT);

  // GPIO-Holds vom letzten Deep-Sleep aufheben, bevor wir die Pins
  // neu ansteuern (sonst ignoriert der Chip unsere digitalWrite-Aufrufe)
  gpio_hold_dis((gpio_num_t)PIN_VBAT_LATCH);
  gpio_hold_dis((gpio_num_t)PIN_EPD_POWER);
  gpio_deep_sleep_hold_dis();
}

void connectWiFi() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(120); // Portal schließt nach 2 Min ohne Eingabe
  data.wifiOk = wm.autoConnect("TempHumPow-Setup");

  if (data.wifiOk) {
    configTime(3600, 3600, "pool.ntp.org", "time.nist.gov"); // MEZ/MESZ Wien
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
      strftime(data.timeStr, sizeof(data.timeStr), "%H:%M", &timeinfo);
    }
  }
}

void readLocalSensor() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // SHTC3 Wakeup-Kommando
  Wire.beginTransmission(0x70);
  Wire.write(0x35); Wire.write(0x17);
  Wire.endTransmission();
  delay(1);

  // Messung starten (normal mode, clock stretching disabled)
  Wire.beginTransmission(0x70);
  Wire.write(0x78); Wire.write(0x66);
  Wire.endTransmission();
  delay(15);

  Wire.requestFrom(0x70, 6);
  if (Wire.available() == 6) {
    uint16_t rawTemp = (Wire.read() << 8) | Wire.read();
    Wire.read(); // CRC überspringen (für Produktivcode prüfen!)
    uint16_t rawHum = (Wire.read() << 8) | Wire.read();
    Wire.read(); // CRC überspringen

    data.tempIn = -45.0f + 175.0f * ((float)rawTemp / 65535.0f);
    data.humIn  = 100.0f * ((float)rawHum / 65535.0f);
  }

  // Sensor wieder schlafen legen
  Wire.beginTransmission(0x70);
  Wire.write(0xB0); Wire.write(0x98);
  Wire.endTransmission();
}

void readBattery() {
  if (BATTERY_ADC_PIN < 0) {
    data.batteryPct = -1;
    return;
  }
  analogReadResolution(12);
  int raw = analogRead(BATTERY_ADC_PIN);
  float vAdc = (raw / 4095.0f) * 3.3f;
  float vBat = vAdc * VOLTAGE_DIVIDER_RATIO;
  float pct = (vBat - BATTERY_MIN_V) / (BATTERY_MAX_V - BATTERY_MIN_V) * 100.0f;
  data.batteryPct = constrain((int)pct, 0, 100);
}

void fetchViennaWeather() {
  if (!data.wifiOk) return;

  HTTPClient http;
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" VIENNA_LAT
               "&longitude=" VIENNA_LON
               "&current=temperature_2m,precipitation_probability"
               "&timezone=Europe%2FVienna";

  http.begin(url);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();

    JsonDocument filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["precipitation_probability"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

    if (!err) {
      data.tempOut = doc["current"]["temperature_2m"] | NAN;
      data.rainOut = doc["current"]["precipitation_probability"] | -1;
    }
  }
  http.end();
}

// ---------------------------------------------------------------------------
// HUD ZEICHNEN (200x200, reines Schwarz/Weiß)
// ---------------------------------------------------------------------------
// Layout 1:1 wie im Mockup: Eckklammern, geteilte Innen/Außen-Anzeige,
// Halbkreis-Gauges, Akkubalken unten, Uhrzeit, "SYS NOMINAL"-Statuszeile.

void drawCornerBrackets() {
  // Vier L-förmige Ecken, je 12px Schenkellänge
  // display.drawLine(4,16, 4,4);   display.drawLine(4,4, 16,4);
  // display.drawLine(184,4, 196,4); display.drawLine(196,4, 196,16);
  // display.drawLine(196,184, 196,196); display.drawLine(196,196, 184,196);
  // display.drawLine(16,196, 4,196); display.drawLine(4,196, 4,184);
}

void drawGauge(int cx, int cy, int r, float value, float minV, float maxV, const char* unit) {
  // Kreis + Teilbogen oben links als "Tick", passend zum Mockup
  // display.drawCircle(cx, cy, r);
  // Bogenlänge proportional zu (value-minV)/(maxV-minV) über 90° zeichnen
  // (Punkt-für-Punkt via sin/cos, da GxEPD2 keine Arc-Primitive hat)
  float pct = constrain((value - minV) / (maxV - minV), 0.0f, 1.0f);
  float startAngle = -90; // Grad, oben
  float sweep = 90 * pct;
  for (float a = startAngle; a <= startAngle + sweep; a += 2) {
    float rad = a * PI / 180.0;
    int x = cx + r * cos(rad);
    int y = cy + r * sin(rad);
    // display.drawPixel(x, y, GxEPD_BLACK);
    // display.drawPixel(x, y-1, GxEPD_BLACK); // etwas dicker
  }
}

void drawBatteryIcon(int x, int y, int pct) {
  // Rahmen 26x12 + Knubbel + gefüllte Segmente je nach Prozent
  // display.drawRect(x, y, 26, 12, GxEPD_BLACK);
  // display.fillRect(x+26, y+4, 3, 4, GxEPD_BLACK);
  int filledSegments = map(constrain(pct, 0, 100), 0, 100, 0, 3);
  for (int i = 0; i < 3; i++) {
    int sx = x + 2 + i * 7;
    if (i < filledSegments) {
      // display.fillRect(sx, y+2, 6, 8, GxEPD_BLACK);
    } else {
      // display.drawRect(sx, y+2, 6, 8, GxEPD_BLACK);
    }
  }
}

void drawWifiBars(int x, int y, bool ok) {
  int heights[4] = {8, 11, 14, 17};
  for (int i = 0; i < 4; i++) {
    int barX = x + i * 5;
    int barY = y + (17 - heights[i]);
    if (ok || i == 0) {
      // display.fillRect(barX, barY, 3, heights[i], GxEPD_BLACK);
    } else {
      // display.drawRect(barX, barY, 3, heights[i], GxEPD_BLACK);
    }
  }
}

void drawHud() {
  // display.setRotation(0);
  // display.fillScreen(GxEPD_WHITE);

  drawCornerBrackets();

  // Header
  // display.setCursor(52, 12); display.setTextSize(1);
  // display.print("TEMPHUMPOW");
  // display.drawLine(10, 21, 190, 21, GxEPD_BLACK);
  // display.drawLine(100, 26, 100, 150, GxEPD_BLACK); // gestrichelt simulieren

  // Links: Innen
  // display.setCursor(14, 34); display.print("INT");
  drawGauge(52, 70, 34, data.tempIn, 0, 40, "C");
  // display.setCursor(38, 74); display.setTextSize(2);
  // display.print(String(data.tempIn, 1));
  // display.setCursor(20, 122); display.setTextSize(1);
  // display.print("RH " + String((int)data.humIn) + "%");

  // Rechts: Wien
  // display.setCursor(112, 34); display.print("WIEN");
  drawGauge(148, 70, 34, data.tempOut, -10, 35, "C");
  // display.setCursor(134, 74); display.setTextSize(2);
  // display.print(String(data.tempOut, 1));
  // display.setCursor(140, 122); display.setTextSize(1);
  // display.print(String(data.rainOut) + "%");

  // Unten: Akku, WLAN, Uhrzeit
  // display.drawLine(10, 140, 190, 140, GxEPD_BLACK);
  drawBatteryIcon(14, 150, data.batteryPct);
  // display.setCursor(50, 160); display.print(String(data.batteryPct) + "%");
  drawWifiBars(86, 146, data.wifiOk);
  // display.setCursor(140, 160); display.print("UPD " + String(data.timeStr));

  // display.drawLine(10, 170, 190, 170, GxEPD_BLACK);
  // display.setCursor(58, 182);
  // display.print(data.wifiOk ? "SYS NOMINAL" : "SYS OFFLINE");

  pushDisplay();
}

// ---------------------------------------------------------------------------
// DEEP SLEEP KONFIGURATION
// ---------------------------------------------------------------------------
void goToSleep() {
  // Panel-Stromversorgung abschalten
  digitalWrite(PIN_EPD_POWER, HIGH);

  // Power-Management-Pins über den Deep Sleep hinweg festhalten,
  // sonst verliert das Board seinen eingeschalteten Zustand
  gpio_hold_en((gpio_num_t)PIN_VBAT_LATCH);
  gpio_hold_en((gpio_num_t)PIN_EPD_POWER);
  gpio_deep_sleep_hold_en();

  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);

  // Manuelles Aufwachen per PWR-Taster (Pegel ggf. anhand Hardware-Test
  // anpassen: 0 = wake bei LOW, 1 = wake bei HIGH)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_PWR_BUTTON_SENSE, 0);

  esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------
// SETUP / LOOP
// ---------------------------------------------------------------------------
void setup() {
  latchBatteryPower(); // MUSS zuerst passieren
  Serial.begin(115200);

  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  Serial.printf("Wakeup Grund: %d (1=Timer, wenn undefined=Erststart)\n", wakeReason);

  initDisplay();
  connectWiFi();
  readLocalSensor();
  readBattery();
  fetchViennaWeather();
  drawHud();

  goToSleep(); // Setup kehrt nie zurück, ESP schläft direkt weiter
}

void loop() {
  // wird nie erreicht (Deep Sleep Restart-Zyklus)
}
