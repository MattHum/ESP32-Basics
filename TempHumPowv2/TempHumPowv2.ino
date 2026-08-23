/*
  TempHumPowv2 - SciFi ePaper Display mit WiFi und Wetter
  Waveshare ESP32-S3-ePaper-1.54G
  
  Zeigt: Innentemperatur, Feuchtigkeit, Batterie
  + Wien Außentemperatur + Regenwahrscheinlichkeit (Open-Meteo)
  Update: bei WLAN-Connect oder alle 5 Min (Deep Sleep optional)

  WLAN-SETUP: Bei erstmaligem Start oder nach Änderung der Zugangsdaten
  öffnet sich automatisch ein Setup-Portal ("TempHumPow-Setup"). Sobald WLAN
  verbunden ist, wird der finale HUD mit allen Daten gezeichnet. Bei vorhandenem
  gespeicherten WLAN erscheint der finale HUD sofort nach dem Start.
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "epaper_driver_bsp.h"

// --- Pins ---
#define PIN_VBAT_LATCH    17
#define PIN_EPD_POWER      6
#define BATTERY_ADC_PIN    4
#define PIN_I2C_SDA       47
#define PIN_I2C_SCL       48
#define PIN_PWR_BUTTON    18

// --- EPD ---
#define EPD_PIN_CS   11
#define EPD_PIN_DC     10
#define EPD_PIN_RST    9
#define EPD_PIN_BUSY   8
#define EPD_PIN_MOSI   13
#define EPD_PIN_SCLK   12
#define EPD_WIDTH   200
#define EPD_HEIGHT   200
#define EPD_BUFFER_LEN ((EPD_WIDTH * EPD_HEIGHT) / 4)

// --- Wien-Daten ---
#define VIENNA_LAT  "48.2082"
#define VIENNA_LON  "16.3738"
#define VIENNA_TZ   "Europe/Vienna"

// --- Globals ---
epaper_driver_display *epd = nullptr;
float gTemp = NAN;
float gHum = NAN;
float gOutTemp = NAN;
int gRain = -1;
int gBatPct = -1;
bool gWifiOk = false;
char gTimeStr[6] = "--:--";

// --- Display primitives ---
void setPx(int x, int y, bool black) {
  if (x < 0 || y < 0 || x >= EPD_WIDTH || y >= EPD_HEIGHT) return;
  epd->EPD_DrawColorPixel(x, y, black ? DRIVER_COLOR_BLACK : DRIVER_COLOR_WHITE);
}

void drawLine(int x0, int y0, int x1, int y1, bool black = true) {
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
  int x = r, y = 0, err = 0;
  while (x >= y) {
    setPx(cx + x, cy + y, black); setPx(cx + y, cy + x, black);
    setPx(cx - y, cy + x, black); setPx(cx - x, cy + y, black);
    setPx(cx - x, cy - y, black); setPx(cx - y, cy - x, black);
    setPx(cx + y, cy - x, black); setPx(cx + x, cy - y, black);
    y++; err += 1 + 2 * y;
    if (2 * (err - x) + 1 > 0) { x--; err += 1 - 2 * x; }
  }
}

void drawArc(int cx, int cy, int r, float startDeg, float sweepDeg, int thickness = 2) {
  for (float a = startDeg; a <= startDeg + sweepDeg; a += 1.0) {
    float rad = a * PI / 180.0;
    int px = cx + round(r * cos(rad));
    int py = cy + round(r * sin(rad));
    for (int t = 0; t < thickness; t++) setPx(px, py - t, true);
  }
}

void drawGauge(int cx, int cy, int r, float value, float minV, float maxV) {
  drawCircle(cx, cy, r);
  float pct = constrain((value - minV) / (maxV - minV), 0.0f, 1.0f);
  if (pct > 0.01f) drawArc(cx, cy, r, -90, 90 * pct, 2);
}

// --- 5x7 Font ---
static const uint8_t* getGlyph(char c) {
  static const uint8_t G_T[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
  static const uint8_t G_E[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
  static const uint8_t G_M[7] = {0x11,0x1B,0x15,0x11,0x11,0x11,0x11};
  static const uint8_t G_P[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
  static const uint8_t G_H[7] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
  static const uint8_t G_C[7] = {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F};
  static const uint8_t G_S[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
  static const uint8_t G_Y[7] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04};
  static const uint8_t G_O[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
  static const uint8_t G_W[7] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11};
  static const uint8_t G_R[7] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
  static const uint8_t G_I[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F};
  static const uint8_t G_N[7] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
  static const uint8_t G_D[7] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
  static const uint8_t G_G[7] = {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F};
  static const uint8_t G_L[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
  static const uint8_t G_A[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
  static const uint8_t G_F[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
  static const uint8_t G_B[7] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};
  static const uint8_t G_U[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
  static const uint8_t G_DEG[7] = {0x06,0x09,0x09,0x06,0,0,0};
  static const uint8_t G_DOT[7] = {0,0,0,0,0,0,0x04};
  static const uint8_t G_COLON[7] = {0,0,0x04,0,0x04,0,0};
  static const uint8_t G_PCT[7] = {0x19,0x1A,0x04,0x04,0x04,0x0B,0x13};
  static const uint8_t G_SPACE[7] = {0,0,0,0,0,0,0};
  switch (c) {
    case 'T': return G_T; case 'E': return G_E; case 'M': return G_M;
    case 'P': return G_P; case 'H': return G_H; case 'C': return G_C;
    case 'S': return G_S; case 'Y': return G_Y; case 'O': return G_O;
    case 'W': return G_W; case 'R': return G_R; case 'I': return G_I;
    case 'N': return G_N; case 'D': return G_D; case 'G': return G_G;
    case 'L': return G_L; case 'A': return G_A; case 'F': return G_F;
    case 'B': return G_B; case 'U': return G_U;
    case '.': return G_DOT; case ':': return G_COLON; case '%': return G_PCT;
    case 'o': return G_DEG;
    default: return G_SPACE;
  }
}

int drawChar(int x, int y, char c, int scale = 1) {
  const uint8_t* g = getGlyph(c);
  for (int r = 0; r < 7; r++)
    for (int col = 0; col < 5; col++)
      if (g[r] & (0x10 >> col))
        fillRect(x + col * scale, y + r * scale, scale, scale, true);
  return 6 * scale;
}

int drawText(int x, int y, const char* t, int scale = 1) {
  int cx = x;
  for (int i = 0; t[i]; i++) cx += drawChar(cx, y, t[i], scale);
  return cx - x;
}

// --- 7-Segment ---
static const uint8_t SEG[10] = {
  0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
  0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011
};

void draw7Seg(int x, int y, int w, int h, int d, int t = 3) {
  if (d < 0 || d > 9) return;
  uint8_t s = SEG[d];
  int my = y + h / 2;
  if (s & 0b1000000) fillRect(x, y, w, t, true);
  if (s & 0b0100000) fillRect(x + w - t, y, t, h / 2, true);
  if (s & 0b0010000) fillRect(x + w - t, y + h / 2, t, h / 2, true);
  if (s & 0b0001000) fillRect(x, y + h - t, w, t, true);
  if (s & 0b0000100) fillRect(x, y + h / 2, t, h / 2, true);
  if (s & 0b0000010) fillRect(x, y, t, h / 2, true);
  if (s & 0b0000001) fillRect(x, y + h / 2 - t / 2, w, t, true);
}

int drawBigNum(int x, int y, float val, int dw, int dh, int t = 3) {
  char buf[8];
  if (isnan(val)) snprintf(buf, sizeof(buf), "--.-");
  else snprintf(buf, sizeof(buf), "%.1f", val);
  int cx = x;
  for (int i = 0; buf[i]; i++) {
    if (buf[i] == '-') { fillRect(cx, y + dh/2 - t/2, dw/2, t, true); cx += dw/2 + 3; }
    else if (buf[i] == '.') { fillRect(cx, y + dh - t, t, t, true); cx += t + 4; }
    else { draw7Seg(cx, y, dw, dh, buf[i] - '0', t); cx += dw + 4; }
  }
  return cx - x;
}

// --- Battery Icon ---
void drawBattery(int x, int y, int pct) {
  drawRect(x, y, 26, 12);
  fillRect(x + 26, y + 4, 3, 4, true);
  int filled = (pct < 0) ? 0 : map(constrain(pct, 0, 100), 0, 100, 0, 3);
  for (int i = 0; i < 3; i++) {
    int sx = x + 2 + i * 7;
    if (i < filled) fillRect(sx, y + 2, 6, 8, true);
    else drawRect(sx, y + 2, 6, 8);
  }
}

// --- Sensor ---
void readSensor() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.beginTransmission(0x70);
  Wire.write(0x35); Wire.write(0x17);
  Wire.endTransmission();
  delay(1);
  Wire.beginTransmission(0x70);
  Wire.write(0x78); Wire.write(0x66);
  Wire.endTransmission();
  delay(15);
  Wire.requestFrom((uint8_t)0x70, (uint8_t)6);
  if (Wire.available() == 6) {
    uint16_t rawT = (Wire.read() << 8) | Wire.read(); Wire.read();
    uint16_t rawH = (Wire.read() << 8) | Wire.read(); Wire.read();
    gTemp = -45.0f + 175.0f * ((float)rawT / 65535.0f);
    gHum = 100.0f * ((float)rawH / 65535.0f);
  }
  Wire.beginTransmission(0x70);
  Wire.write(0xB0); Wire.write(0x98);
  Wire.endTransmission();
}

// --- Battery ---
void readBattery() {
  analogReadResolution(12);
  int raw = analogRead(BATTERY_ADC_PIN);
  float vBat = (raw / 4095.0f) * 3.3f * 2.0f;
  float pct = (vBat - 3.3f) / (4.2f - 3.3f) * 100.0f;
  gBatPct = constrain((int)pct, 0, 100);
}

// --- WLAN & Wetter ---
void connectWiFi() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  gWifiOk = wm.autoConnect("TempHumPow-Setup");
  if (gWifiOk) {
    setenv("TZ", VIENNA_TZ, 1);
    tzset();
    configTime(0, 0, "pool.ntp.org");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
      strftime(gTimeStr, sizeof(gTimeStr), "%H:%M", &timeinfo);
    }
  }
}

void fetchViennaWeather() {
  if (!gWifiOk) return;
  
  HTTPClient http;
  char url[256];
  snprintf(url, sizeof(url),
    "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
    "&current_weather=true&current=relative_humidity_2m"
    "&timezone=Europe%%2FVienna",
    VIENNA_LAT, VIENNA_LON);
  
  Serial.printf("[WETT] URL: %s\n", url);
  http.begin(url);
  int code = http.GET();
  Serial.printf("[WETT] HTTP Status: %d\n", code);
  
  if (code == 200) {
    String payload = http.getString();
    Serial.printf("[WETT] Payload: %.200s...\n", payload.c_str());
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      gOutTemp = doc["current_weather"]["temperature"];
      gRain = doc["current_weather"]["weathercode"];
      Serial.printf("[WETT] Temp: %.1f, Code: %d\n", gOutTemp, gRain);
    } else {
      Serial.printf("[WETT] JSON Error: %s\n", error.c_str());
    }
  } else {
    Serial.printf("[WETT] HTTP Fehler: %d\n", code);
  }
  http.end();
}

// --- HUD Drawing ---
void drawHud() {
  drawLine(0, 0, 12, 0); drawLine(0, 0, 0, 12);
  drawLine(188, 0, 200, 0); drawLine(200, 0, 200, 12);
  drawLine(0, 188, 0, 200); drawLine(0, 200, 12, 200);
  drawLine(200, 188, 200, 200); drawLine(188, 200, 200, 200);

  drawLine(0, 58, 200, 58);
  drawLine(0, 126, 200, 126);

  drawText(6, 4, "INNEN", 2);
  int w1 = drawBigNum(6, 24, gTemp, 16, 28, 2);
  drawText(8 + w1, 28, "oC", 2);

  drawText(106, 4, "WIEN", 2);
  int w2 = drawBigNum(106, 24, gOutTemp, 16, 28, 2);
  drawText(108 + w2, 28, "oC", 2);

  drawText(6, 64, "FEUCHT", 2);
  if (!isnan(gHum)) {
    char hbuf[8];
    snprintf(hbuf, sizeof(hbuf), "%d", (int)gHum);
    int hw = drawText(6, 82, hbuf, 3);
    drawText(8 + hw, 88, "%", 2);
  } else {
    drawText(6, 82, "--", 3);
  }

  drawBattery(6, 132, gBatPct);
  if (gBatPct >= 0) {
    char bbuf[8];
    snprintf(bbuf, sizeof(bbuf), "%d%%", gBatPct);
    drawText(38, 132, bbuf, 2);
  }

  if (gWifiOk) {
    drawText(130, 132, gTimeStr, 2);
  } else {
    drawText(130, 132, "WIFI?", 2);
  }

  epd->EPD_Display();
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== TempHumPowv2 START ===");

  // Power
  pinMode(PIN_VBAT_LATCH, OUTPUT);
  digitalWrite(PIN_VBAT_LATCH, HIGH);
  pinMode(PIN_EPD_POWER, OUTPUT);
  digitalWrite(PIN_EPD_POWER, LOW);

  // Display Treiber
  custom_lcd_spi_t pins;
  pins.cs = EPD_PIN_CS;
  pins.dc = EPD_PIN_DC;
  pins.rst = EPD_PIN_RST;
  pins.busy = EPD_PIN_BUSY;
  pins.mosi = EPD_PIN_MOSI;
  pins.scl = EPD_PIN_SCLK;
  pins.spi_host = SPI2_HOST;
  pins.buffer_len = EPD_BUFFER_LEN;

  epd = new epaper_driver_display(EPD_WIDTH, EPD_HEIGHT, pins);
  epd->EPD_Init();
  epd->EPD_Clear();
  epd->EPD_Display();
  Serial.println("[EPD] Init OK");

  // Sensoren sofort lesen
  Serial.println("[SENS] Lies SHTC3...");
  readSensor();
  Serial.printf("[SENS] Temp: %.1f C, Hum: %.1f %%\n", gTemp, gHum);

  readBattery();
  Serial.printf("[SENS] Battery: %d%%\n", gBatPct);

  // HUD sofort zeichnen (mit [w] da WiFi noch nicht verbunden)
  Serial.println("[HUD] Zeichne Initial-HUD...");
  drawHud();

  // WLAN starten (nach erstem Display-Update)
  Serial.println("[WLAN] Verbinde...");
  connectWiFi();
  Serial.printf("[WLAN] Status: %s\n", gWifiOk ? "ok" : "failed");

  // Wetter abrufen wenn WiFi verbunden
  if (gWifiOk) {
    Serial.println("[WETT] Hole Wien-Wetter...");
    fetchViennaWeather();
    Serial.printf("[WETT] Außentemp: %.1f C, Regen: %d\n", gOutTemp, gRain);
  }

  // HUD neu zeichnen mit finalen Daten
  Serial.println("[HUD] Zeichne finalen HUD...");
  drawHud();
  Serial.println("[HUD] HUD gezeichnet");

  Serial.println("=== Setup fertig ===");
}

void loop() {}