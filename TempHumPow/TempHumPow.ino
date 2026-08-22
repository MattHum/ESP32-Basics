/*
  ============================================================================
  TempHumPow — SciFi-HUD Anzeige für Waveshare ESP32-S3-ePaper-1.54G
  ============================================================================
  Zeigt an:
    - Innentemperatur + Feuchtigkeit (onboard SHTC3)
    - Wien Außentemperatur + Regenwahrscheinlichkeit (Open-Meteo API)
    - Akkustand (ADC-Batteriemessung)
  Update: alle 5 Minuten (Deep Sleep Timer) ODER sofort per PWR-Taster.

  WLAN-SETUP-VERHALTEN: Ist noch kein WLAN gespeichert (Erststart oder nach
  Zugangsdaten-Änderung), öffnet WiFiManager automatisch ein Setup-Portal
  ("TempHumPow-Setup"). Genau in diesem Moment zeichnet der Sketch sofort
  einen Hinweis-Screen mit den bereits verfügbaren lokalen Werten (Temp,
  Feuchte, Akku) + WLAN-Verbindungsanleitung. Sobald WLAN erfolgreich
  konfiguriert (oder der Portal-Timeout von 2 Min erreicht) ist, wird
  einmal der finale HUD mit allen Daten gezeichnet. Bei bereits gespeichertem
  WLAN (normaler 5-Minuten-Zyklus) erscheint der Setup-Screen nicht, es wird
  nur der finale HUD gezeichnet.

  DISPLAY-TREIBER: Nutzt den echten "epaper_driver_bsp" Treiber (2 Bit/Pixel,
  Pixel-Zugriff nur über EPD_DrawColorPixel). Alle Linien-, Rechteck-,
  Kreis-, Text- und 7-Segment-Ziffern-Funktionen in diesem Sketch sind
  selbst geschrieben, da der Treiber selbst keine Grafik-Primitiven bietet.

  HARDWARE-PINS: Alle projektrelevanten Pins sind laut offizieller Waveshare-
  GPIO-Tabelle eingetragen und bestätigt:
    EPD: CS=11, DC=10, RST=9, BUSY=8, SDI(MOSI)=13, SCLK=12, EPD3V3_EN=6
    Battery: BAT_ADC=4 (Teiler 1:2, exakt), BAT_Control=17, BAT_KEY=18
    I2C: SDA=47, SCL=48 (RTC/SHTC3/EPD-Touch teilen sich den Bus)
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
#include "epaper_driver_bsp.h"

// ---------------------------------------------------------------------------
// BOARD-SPEZIFISCHES POWER-MANAGEMENT (ESP32-S3-ePaper-1.54G)
// ---------------------------------------------------------------------------
#define PIN_PWR_BUTTON_SENSE   18   // liest PWR-Tasterzustand
#define PIN_VBAT_LATCH         17   // MUSS beim Boot sofort HIGH -> hält Batteriestrom
#define PIN_EPD_POWER           6   // LOW = Panel bekommt Strom, HIGH = Panel aus

// I2C (SHTC3 + PCF85063 RTC)
#define PIN_I2C_SDA            47
#define PIN_I2C_SCL            48

// Battery-ADC: GPIO4, Spannungsteiler R21(200K, pull-up)/R38(200K pull-down)
// laut Waveshare-Doku: VBAT = VADC x 2 -> Verhältnis ist exakt, keine
// Kalibrierung nötig (ADC-Nichtlinearität am oberen/unteren Rand ausgenommen)
#define BATTERY_ADC_PIN         4
#define VOLTAGE_DIVIDER_RATIO   2.0f
#define BATTERY_MIN_V           3.3f
#define BATTERY_MAX_V           4.2f

// ---------------------------------------------------------------------------
// EPD SPI PINS (laut offizieller Waveshare-GPIO-Tabelle, bestätigt)
// ---------------------------------------------------------------------------
#define EPD_PIN_CS     11   // EPD_CS
#define EPD_PIN_DC     10   // EPD_D/C
#define EPD_PIN_RST     9   // EPD_RST
#define EPD_PIN_BUSY    8   // EPD_BUSY
#define EPD_PIN_MOSI   13   // EPD_SDI
#define EPD_PIN_SCLK   12   // EPD_SCLK
#define EPD_SPI_HOST  SPI2_HOST

#define EPD_WIDTH   200
#define EPD_HEIGHT  200
#define EPD_BUFFER_LEN  ((EPD_WIDTH * EPD_HEIGHT) / 4)  // 2 Bit/Pixel, 4 Pixel/Byte

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

// Vorwärtsdeklarationen (Definitionen weiter unten im Abschnitt "HUD ZEICHNEN")
void drawSetupScreen();
void drawHud();

// ---------------------------------------------------------------------------
// DISPLAY TREIBER (echter Treiber aus epaper_driver_bsp.h/.cpp)
// ---------------------------------------------------------------------------
custom_lcd_spi_t epd_pins = {
  .cs   = EPD_PIN_CS,
  .dc   = EPD_PIN_DC,
  .rst  = EPD_PIN_RST,
  .busy = EPD_PIN_BUSY,
  .mosi = EPD_PIN_MOSI,
  .scl  = EPD_PIN_SCLK,
  .spi_host  = EPD_SPI_HOST,
  .buffer_len = EPD_BUFFER_LEN
};

epaper_driver_display *epd = nullptr;

void initDisplay() {
  Serial.println("[EPD] Power-On + Warte auf Panel-Stabilisierung...");
  digitalWrite(PIN_EPD_POWER, LOW);
  delay(500);
  Serial.println("[EPD] Erzeuge Treiber-Objekt...");
  epd = new epaper_driver_display(EPD_WIDTH, EPD_HEIGHT, epd_pins);
  Serial.println("[EPD] Starte EPD_Init...");
  epd->EPD_Init();
  Serial.println("[EPD] EPD_Init abgeschlossen");
  epd->EPD_Clear();
  Serial.println("[EPD] Clear abgeschlossen, starte Display-Refresh...");
  epd->EPD_Display();
  Serial.println("[EPD] Display-Refresh abgeschlossen");
}

void pushDisplay() {
  Serial.println("[EPD] Sende Buffer + starte Refresh (~20s, ggf. Busy-Timeout nach 30s beachten)...");
  epd->EPD_Display(); // sendet Buffer + löst Refresh aus (~20s, kein Partial Refresh möglich)
  Serial.println("[EPD] Refresh-Aufruf zurückgekehrt");
}

// ---------------------------------------------------------------------------
// EIGENE GRAFIK-PRIMITIVEN (der Treiber kann nur EPD_DrawColorPixel)
// ---------------------------------------------------------------------------
void setPx(int x, int y, bool black) {
  if (x < 0 || y < 0 || x >= EPD_WIDTH || y >= EPD_HEIGHT) return;
  epd->EPD_DrawColorPixel(x, y, black ? DRIVER_COLOR_BLACK : DRIVER_COLOR_WHITE);
}

void drawLine(int x0, int y0, int x1, int y1, bool black = true) {
  // Bresenham
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    setPx(x0, y0, black);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void drawRect(int x, int y, int w, int h, bool black = true) {
  drawLine(x, y, x + w - 1, y, black);
  drawLine(x, y + h - 1, x + w - 1, y + h - 1, black);
  drawLine(x, y, x, y + h - 1, black);
  drawLine(x + w - 1, y, x + w - 1, y + h - 1, black);
}

void fillRect(int x, int y, int w, int h, bool black = true) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++)
      setPx(i, j, black);
}

void drawCircle(int cx, int cy, int r, bool black = true) {
  // Midpoint-Circle-Algorithmus (Umriss)
  int x = r, y = 0, err = 0;
  while (x >= y) {
    setPx(cx + x, cy + y, black); setPx(cx + y, cy + x, black);
    setPx(cx - y, cy + x, black); setPx(cx - x, cy + y, black);
    setPx(cx - x, cy - y, black); setPx(cx - y, cy - x, black);
    setPx(cx + y, cy - x, black); setPx(cx + x, cy - y, black);
    y += 1;
    err += 1 + 2 * y;
    if (2 * (err - x) + 1 > 0) { x -= 1; err += 1 - 2 * x; }
  }
}

// Bogen (Gauge-Tick) über "sweepDeg" Grad ab "startDeg", Startpunkt oben = -90°
void drawArc(int cx, int cy, int r, float startDeg, float sweepDeg, int thickness = 2) {
  for (float a = startDeg; a <= startDeg + sweepDeg; a += 1.0) {
    float rad = a * PI / 180.0;
    int x = cx + round(r * cos(rad));
    int y = cy + round(r * sin(rad));
    for (int t = 0; t < thickness; t++) setPx(x, y - t, true);
  }
}

// --- Minimalistischer 5x7-Blockfont, nur die im HUD benötigten Zeichen ---
// Eigene, einfache Blockglyphen (kein bestehendes Font-Set kopiert).
static const uint8_t* getGlyph(char c) {
  static const uint8_t G_I[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F};
  static const uint8_t G_N[7] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
  static const uint8_t G_T[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
  static const uint8_t G_W[7] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11};
  static const uint8_t G_E[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
  static const uint8_t G_H[7] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
  static const uint8_t G_R[7] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
  static const uint8_t G_D[7] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
  static const uint8_t G_G[7] = {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F};
  static const uint8_t G_C[7] = {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F};
  static const uint8_t G_S[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
  static const uint8_t G_Y[7] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04};
  static const uint8_t G_O[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
  static const uint8_t G_M[7] = {0x11,0x1B,0x15,0x11,0x11,0x11,0x11};
  static const uint8_t G_P[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
  static const uint8_t G_U[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
  static const uint8_t G_F[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
  static const uint8_t G_L[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
  static const uint8_t G_A[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
  static const uint8_t G_DOT[7]   = {0,0,0,0,0,0,0x04};
  static const uint8_t G_COLON[7] = {0,0,0x04,0,0x04,0,0};
  static const uint8_t G_DASH[7]  = {0,0,0,0x0E,0,0,0};
  static const uint8_t G_PCT[7]   = {0x19,0x1A,0x04,0x04,0x04,0x0B,0x13};
  static const uint8_t G_LBRACKET[7] = {0x0C,0x08,0x08,0x08,0x08,0x08,0x0C};
  static const uint8_t G_RBRACKET[7] = {0x06,0x02,0x02,0x02,0x02,0x02,0x06};
  static const uint8_t G_SPACE[7] = {0,0,0,0,0,0,0};
  switch (c) {
    case 'I': return G_I;  case 'N': return G_N;  case 'T': return G_T;
    case 'W': return G_W;  case 'E': return G_E;  case 'H': return G_H;
    case 'R': return G_R;  case 'D': return G_D;  case 'G': return G_G;
    case 'C': return G_C;  case 'S': return G_S;  case 'Y': return G_Y;
    case 'O': return G_O;  case 'M': return G_M;  case 'P': return G_P;
    case 'U': return G_U;  case 'F': return G_F;  case 'L': return G_L;
    case 'A': return G_A;  case '.': return G_DOT; case ':': return G_COLON;
    case '-': return G_DASH; case '%': return G_PCT;
    case '[': return G_LBRACKET; case ']': return G_RBRACKET;
    default:  return G_SPACE;
  }
}

int drawChar(int x, int y, char c, int scale = 1) {
  const uint8_t* glyph = getGlyph(toupper(c));
  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 5; col++) {
      if (glyph[row] & (0x10 >> col)) {
        fillRect(x + col * scale, y + row * scale, scale, scale, true);
      }
    }
  }
  return 6 * scale; // Zeichenbreite inkl. 1px Abstand, skaliert
}

int drawText(int x, int y, const char* text, int scale = 1) {
  int cursorX = x;
  for (int i = 0; text[i] != '\0'; i++) {
    cursorX += drawChar(cursorX, y, text[i], scale);
  }
  return cursorX - x; // Gesamtbreite
}

// --- 7-Segment-Ziffern für große Messwerte ---
// Segmente: a=oben, b=oben-rechts, c=unten-rechts, d=unten, e=unten-links, f=oben-links, g=mitte
static const uint8_t SEVEN_SEG[10] = {
  0b1111110, // 0: a b c d e f
  0b0110000, // 1: b c
  0b1101101, // 2: a b g e d
  0b1111001, // 3: a b g c d
  0b0110011, // 4: f g b c
  0b1011011, // 5: a f g c d
  0b1011111, // 6: a f g e c d
  0b1110000, // 7: a b c
  0b1111111, // 8: alle
  0b1111011  // 9: a b c d f g
};

void drawSevenSegDigit(int x, int y, int w, int h, int digit, int thick = 3) {
  if (digit < 0 || digit > 9) return;
  uint8_t seg = SEVEN_SEG[digit];
  int midY = y + h / 2;
  if (seg & 0b1000000) fillRect(x, y, w, thick, true);                          // a: oben
  if (seg & 0b0100000) fillRect(x + w - thick, y, thick, h / 2, true);          // b: oben-rechts
  if (seg & 0b0010000) fillRect(x + w - thick, midY, thick, h / 2, true);       // c: unten-rechts
  if (seg & 0b0001000) fillRect(x, y + h - thick, w, thick, true);              // d: unten
  if (seg & 0b0000100) fillRect(x, midY, thick, h / 2, true);                   // e: unten-links
  if (seg & 0b0000010) fillRect(x, y, thick, h / 2, true);                      // f: oben-links
  if (seg & 0b0000001) fillRect(x, midY - thick / 2, w, thick, true);           // g: mitte
}

// Zeichnet eine Zahl wie "23.4" oder "-4.0" mit 7-Segment-Ziffern
int drawBigNumber(int x, int y, float value, int digitW, int digitH, int thick = 3) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%.1f", value);
  int cursorX = x;
  for (int i = 0; buf[i] != '\0'; i++) {
    if (buf[i] == '-') {
      fillRect(cursorX, y + digitH / 2 - thick / 2, digitW / 2, thick, true);
      cursorX += digitW / 2 + 3;
    } else if (buf[i] == '.') {
      fillRect(cursorX, y + digitH - thick, thick, thick, true);
      cursorX += thick + 4;
    } else {
      drawSevenSegDigit(cursorX, y, digitW, digitH, buf[i] - '0', thick);
      cursorX += digitW + 4;
    }
  }
  return cursorX - x;
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

void configModeCallback(WiFiManager *myWiFiManager) {
  // Wird von WiFiManager genau dann aufgerufen, wenn kein gespeichertes
  // WLAN gefunden wurde und das Setup-Portal (AP) gestartet ist.
  Serial.println("[WiFi] Setup-Portal aktiv -> zeichne Hinweis-Screen mit lokalen Werten");
  drawSetupScreen();
}

void connectWiFi() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(120); // Portal schließt nach 2 Min ohne Eingabe
  wm.setAPCallback(configModeCallback);
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
  drawLine(4, 16, 4, 4);    drawLine(4, 4, 16, 4);
  drawLine(184, 4, 196, 4); drawLine(196, 4, 196, 16);
  drawLine(196, 184, 196, 196); drawLine(196, 196, 184, 196);
  drawLine(16, 196, 4, 196);    drawLine(4, 196, 4, 184);
}

// Kreis + Teilbogen als "Tick", proportional zu value im Bereich [minV, maxV]
void drawGauge(int cx, int cy, int r, float value, float minV, float maxV) {
  drawCircle(cx, cy, r);
  float pct = constrain((value - minV) / (maxV - minV), 0.0f, 1.0f);
  drawArc(cx, cy, r, -90, 90 * pct, 2);
}

void drawBatteryIcon(int x, int y, int pct) {
  drawRect(x, y, 26, 12);
  fillRect(x + 26, y + 4, 3, 4, true); // Pluspol-Knubbel
  int filledSegments = map(constrain(pct, 0, 100), 0, 100, 0, 3);
  for (int i = 0; i < 3; i++) {
    int sx = x + 2 + i * 7;
    if (i < filledSegments) fillRect(sx, y + 2, 6, 8, true);
    else drawRect(sx, y + 2, 6, 8);
  }
}

void drawWifiBars(int x, int y, bool ok) {
  int heights[4] = {8, 11, 14, 17};
  for (int i = 0; i < 4; i++) {
    int barX = x + i * 5;
    int barY = y + (17 - heights[i]);
    if (ok || i == 0) fillRect(barX, barY, 3, heights[i], true);
    else drawRect(barX, barY, 3, heights[i]);
  }
}

// Wird angezeigt, solange kein WLAN gespeichert ist / das Setup-Portal läuft.
// Zeigt die lokalen Werte (Temp/Feuchte/Akku) großzügig, plus ein kleines
// "[W]"-Badge oben rechts als dezenten Hinweis auf den WLAN-Setup-Modus.
void drawSetupScreen() {
  epd->EPD_Clear();

  drawCornerBrackets();

  drawText(56, 8, "TEMPHUMPOW", 1);
  drawText(168, 8, "[W]", 1);
  drawLine(10, 21, 190, 21);

  drawText(14, 26, "INT", 1);
  drawGauge(52, 70, 34, data.tempIn, 0, 40);
  drawBigNumber(24, 58, data.tempIn, 12, 20, 3);
  drawRect(20, 112, 64, 14);
  { char buf[16]; snprintf(buf, sizeof(buf), "RH %d%%", (int)data.humIn);
    drawText(30, 116, buf, 1); }

  drawLine(10, 140, 190, 140);
  drawBatteryIcon(14, 150, data.batteryPct);
  { char buf[8]; snprintf(buf, sizeof(buf), "%d%%", data.batteryPct);
    drawText(50, 154, buf, 1); }
  drawWifiBars(86, 146, false);

  drawLine(10, 170, 190, 170);
  drawText(58, 180, "SYS OFFLINE", 1);

  pushDisplay();
}

void drawHud() {
  epd->EPD_Clear(); // Buffer auf Weiß zurücksetzen (falls vorher Setup-Screen lief)

  drawCornerBrackets();

  // Header
  drawText(56, 8, "TEMPHUMPOW", 1);
  drawLine(10, 21, 190, 21);
  for (int yy = 26; yy < 150; yy += 4) setPx(100, yy, true); // gestrichelte Trennlinie

  // Links: Innen (SHTC3)
  drawText(14, 26, "INT", 1);
  drawGauge(52, 70, 34, data.tempIn, 0, 40);
  drawBigNumber(24, 58, data.tempIn, 12, 20, 3);
  drawRect(20, 112, 64, 14);
  { char buf[16]; snprintf(buf, sizeof(buf), "RH %d%%", (int)data.humIn);
    drawText(30, 116, buf, 1); }

  // Rechts: Wien (Open-Meteo)
  drawText(112, 26, "WIEN", 1);
  drawGauge(148, 70, 34, data.tempOut, -10, 35);
  drawBigNumber(120, 58, data.tempOut, 12, 20, 3);
  drawRect(116, 112, 64, 14);
  { char buf[16]; snprintf(buf, sizeof(buf), "%d%%", data.rainOut);
    drawText(150, 116, buf, 1); }

  // Unten: Akku, WLAN, Uhrzeit
  drawLine(10, 140, 190, 140);
  drawBatteryIcon(14, 150, data.batteryPct);
  { char buf[8]; snprintf(buf, sizeof(buf), "%d%%", data.batteryPct);
    drawText(50, 154, buf, 1); }
  drawWifiBars(86, 146, data.wifiOk);
  { char buf[16]; snprintf(buf, sizeof(buf), "UPD%s", data.timeStr);
    drawText(130, 154, buf, 1); }

  drawLine(10, 170, 190, 170);
  drawText(58, 180, data.wifiOk ? "SYS NOMINAL" : "SYS OFFLINE", 1);

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
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== TempHumPow START ===");

  latchBatteryPower();
  Serial.println("[SETUP] latchBatteryPower OK");

  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  Serial.printf("Wakeup Grund: %d (1=Timer, wenn undefined=Erststart)\n", wakeReason);

  Serial.println("[SETUP] initDisplay() ...");
  initDisplay();
  Serial.println("[SETUP] initDisplay() OK");

  Serial.println("[SETUP] readLocalSensor() ...");
  readLocalSensor();
  Serial.printf("[SETUP] SHTC3: tempIn=%.1f humIn=%.1f\n", data.tempIn, data.humIn);

  Serial.println("[SETUP] readBattery() ...");
  readBattery();
  Serial.printf("[SETUP] Battery: %d%%\n", data.batteryPct);

  Serial.println("[SETUP] connectWiFi() ...");
  connectWiFi(); // zeichnet bei Bedarf via Callback bereits den Setup-Hinweis-Screen
  Serial.printf("[SETUP] connectWiFi() OK, wifiOk=%d\n", data.wifiOk);

  Serial.println("[SETUP] fetchViennaWeather() ...");
  fetchViennaWeather();
  Serial.printf("[SETUP] Wien: tempOut=%.1f rainOut=%d\n", data.tempOut, data.rainOut);

  Serial.println("[SETUP] drawHud() ...");
  drawHud();
  Serial.println("[SETUP] drawHud() OK, gehe in Deep Sleep");

  goToSleep(); // Setup kehrt nie zurück, ESP schläft direkt weiter
}

void loop() {
  // wird nie erreicht (Deep Sleep Restart-Zyklus)
}
