/*
  TempHumPowv3 - SciFi ePaper Display mit WiFi, Wetter und Sprachausgabe
  Waveshare ESP32-S3-ePaper-1.54G
  
  Zeigt: Innentemperatur, Feuchtigkeit, Batterie
  + Wien Außentemperatur + Luftfeuchtigkeit (Open-Meteo)
  + Sprachausgabe via Flite TTS ( Englisch )
  
  Audio: ES8311 Codec via I2S, Verstärker GPIO42/46
  Taster: GPIO18 (BOOT) zum manuellen Auslösen der Sprachausgabe

  WLAN-SETUP: Bei erstmaligem Start oder nach Änderung der Zugangsdaten
  öffnet sich automatisch ein Setup-Portal ("TempHumPow-Setup").
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "epaper_driver_bsp.h"

// --- Flite TTS + Audio Tools ---
#include "flite_arduino.h"
#include "AudioTools.h"

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

// --- Audio (ES8311 + I2S) ---
#define I2S_MCK_PIN    14
#define I2S_BCK_PIN    15
#define I2S_LRCK_PIN   38
#define I2S_DOUT_PIN   45
#define PA_CTRL_PIN    46
#define AUDIO_PWR_PIN  42

// --- Wien-Daten ---
#define VIENNA_LAT  "48.2082"
#define VIENNA_LON  "16.3738"
#define VIENNA_TZ   "Europe/Vienna"

// --- Globals ---
epaper_driver_display *epd = nullptr;
float gTemp = NAN;
float gHum = NAN;
float gOutTemp = NAN;
float gOutHum = NAN;
int gRain = -1;
int gBatPct = -1;
bool gWifiOk = false;
char gTimeStr[6] = "--:--";

// --- Audio Globals ---
I2SStream i2sOut;
Flite flite;
bool audioReady = false;

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
  static const uint8_t G_0[7] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
  static const uint8_t G_1[7] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
  static const uint8_t G_2[7] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
  static const uint8_t G_3[7] = {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E};
  static const uint8_t G_4[7] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
  static const uint8_t G_5[7] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E};
  static const uint8_t G_6[7] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E};
  static const uint8_t G_7[7] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
  static const uint8_t G_8[7] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
  static const uint8_t G_9[7] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C};
  switch (c) {
    case 'T': return G_T; case 'E': return G_E; case 'M': return G_M;
    case 'P': return G_P; case 'H': return G_H; case 'C': return G_C;
    case 'S': return G_S; case 'Y': return G_Y; case 'O': return G_O;
    case 'W': return G_W; case 'R': return G_R; case 'I': return G_I;
    case 'N': return G_N; case 'D': return G_D; case 'G': return G_G;
    case 'L': return G_L; case 'A': return G_A; case 'F': return G_F;
    case 'B': return G_B; case 'U': return G_U;
    case '0': return G_0; case '1': return G_1; case '2': return G_2;
    case '3': return G_3; case '4': return G_4; case '5': return G_5;
    case '6': return G_6; case '7': return G_7; case '8': return G_8;
    case '9': return G_9;
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
  drawRect(x, y, 18, 12);
  fillRect(x + 18, y + 4, 3, 4, true);
  int filled = (pct < 0) ? 0 : map(constrain(pct, 0, 100), 0, 100, 0, 3);
  for (int i = 0; i < 3; i++) {
    int sx = x + 2 + i * 5;
    if (i < filled) fillRect(sx, y + 2, 4, 8, true);
    else drawRect(sx, y + 2, 4, 8);
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
    Serial.printf("[SHTC3] OK rawT=%u rawH=%u T=%.1f H=%.1f\n", rawT, rawH, gTemp, gHum);
  } else {
    Serial.printf("[SHTC3] FAIL avail=%d\n", Wire.available());
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
  Serial.printf("[BATT] raw=%d vBat=%.2fV pct_raw=%.1f gBatPct=%d\n", raw, vBat, pct, gBatPct);
}

// --- Audio / TTS ---
void initAudio() {
  Serial.println("[AUDIO] Init ES8311 + I2S...");

  // Verstärker einschalten
  pinMode(PA_CTRL_PIN, OUTPUT);
  pinMode(AUDIO_PWR_PIN, OUTPUT);
  digitalWrite(PA_CTRL_PIN, HIGH);
  digitalWrite(AUDIO_PWR_PIN, LOW);  // LOW = Verstärker AN
  delay(200);

  // I2S konfigurieren: 8kHz, 16bit, Mono (Flite-Ausgabe)
  auto cfg = i2sOut.defaultConfig(TX_MODE);
  cfg.sample_rate = 8000;
  cfg.channels = 1;
  cfg.bits_per_sample = 16;
  cfg.pin_bck = I2S_BCK_PIN;
  cfg.pin_ws = I2S_LRCK_PIN;
  cfg.pin_data = I2S_DOUT_PIN;
  cfg.pin_mck = I2S_MCK_PIN;
  i2sOut.begin(cfg);

  // Flite initialisieren
  flite.begin(i2sOut);
  flite.setVoice(register_cmu_us_kal());
  audioReady = true;
  Serial.println("[AUDIO] Flite TTS bereit (cmu_us_kal)");
}

void speakWeather() {
  if (!audioReady) {
    Serial.println("[TTS] Audio nicht bereit");
    return;
  }

  char sentence[256];

  if (gWifiOk && !isnan(gOutTemp) && !isnan(gOutHum)) {
    // Wien-Daten verfügbar: temperatur + feuchtigkeit
    snprintf(sentence, sizeof(sentence),
      "In Vienna it is currently %.1f degrees Celsius "
      "and the humidity is %d percent.",
      gOutTemp, (int)gOutHum);
  } else if (!isnan(gTemp) && !isnan(gHum)) {
    // Nur Innenwerte
    snprintf(sentence, sizeof(sentence),
      "The indoor temperature is %.1f degrees Celsius "
      "and the humidity is %d percent.",
      gTemp, (int)gHum);
  } else {
    Serial.println("[TTS] Keine Daten zum Sprechen");
    return;
  }

  Serial.printf("[TTS] Spreche: %s\n", sentence);
  flite.say(sentence);
  Serial.println("[TTS] Fertig");
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
    "&current=temperature_2m,relative_humidity_2m,weather_code"
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
      gOutTemp = doc["current"]["temperature_2m"];
      gRain = doc["current"]["weather_code"];
      gOutHum = doc["current"]["relative_humidity_2m"];
      Serial.printf("[WETT] Temp: %.1f, Hum: %.1f, Code: %d\n", gOutTemp, gOutHum, gRain);
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

  // --- Header ---
  drawText(52, 2, "ESP32S3/08-2026", 1);
  drawLine(0, 11, 200, 11);

  // Spaltentrenner INNEN|WIEN (Temp + Feucht)
  drawLine(100, 13, 100, 132);

  // --- TEMPERATUR (y=13-58) ---
  drawLine(0, 58, 200, 58);
  drawText(4, 14, "INNEN", 2);
  drawBigNum(4, 32, gTemp, 13, 24, 2);
  drawText(78, 38, "oC", 2);
  drawText(104, 14, "WIEN", 2);
  drawBigNum(104, 32, gOutTemp, 13, 24, 2);
  drawText(178, 38, "oC", 2);

  // --- FEUCHTIGKEIT (y=58-132) ---
  drawLine(0, 132, 200, 132);
  drawText(4, 64, "FEUCHT", 2);
  drawBigNum(4, 86, gHum, 13, 24, 2);
  drawText(68, 92, "%", 2);
  drawText(104, 64, "WIEN", 2);
  drawBigNum(104, 86, gOutHum, 13, 24, 2);
  drawText(168, 92, "%", 2);

  // --- BATTERY + WIFI (y=134-200) ---
  drawBattery(4, 148, gBatPct);
  { char bbuf[8];
    snprintf(bbuf, sizeof(bbuf), "%d%%", gBatPct);
    drawText(28, 148, bbuf, 2); }
  drawText(140, 148, "WIFI", 1);
  if (gWifiOk) {
    drawText(140, 162, gTimeStr, 2);
  } else {
    drawText(140, 162, "?", 2);
  }

  // Taster-Hinweis
  if (audioReady) {
    drawText(4, 180, "TTS:BOOT", 1);
  }

  epd->EPD_Display();
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== TempHumPowv3 START ===");

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

  // Audio initialisieren
  initAudio();

  // HUD sofort zeichnen
  Serial.println("[HUD] Zeichne Initial-HUD...");
  drawHud();

  // WLAN starten
  Serial.println("[WLAN] Verbinde...");
  connectWiFi();
  if (!gWifiOk && WiFi.status() == WL_CONNECTED) {
    gWifiOk = true;
    Serial.println("[WLAN] WiFi.status() ist connected!");
  }
  Serial.printf("[WLAN] Status: %s, WiFi.status()=%d\n", gWifiOk ? "ok" : "failed", WiFi.status());

  // Wetter abrufen wenn WiFi verbunden
  if (gWifiOk) {
    Serial.println("[WETT] Hole Wien-Wetter...");
    fetchViennaWeather();
    Serial.printf("[WETT] Außentemp: %.1f C, Feucht: %.1f%%, Regen: %d\n", gOutTemp, gOutHum, gRain);
  }

  // HUD neu zeichnen mit finalen Daten
  Serial.println("[HUD] Zeichne finalen HUD...");
  drawHud();
  Serial.println("[HUD] HUD gezeichnet");

  // Sprachausgabe: automatisch nach Wetter-Update
  if (gWifiOk && !isnan(gOutTemp)) {
    Serial.println("[TTS] Automatische Sprachausgabe...");
    speakWeather();
  }

  Serial.println("=== Setup fertig ===");
  Serial.println("[LOOP] Taster drücken für TTS...");
}

// --- Loop: Taster-Handling ---
void loop() {
  // BOOT-Taster (GPIO18) prüfen: LOW = gedrückt
  if (digitalRead(PIN_PWR_BUTTON) == LOW) {
    delay(50);  // Entprellen
    if (digitalRead(PIN_PWR_BUTTON) == LOW) {
      Serial.println("[TTS] Taster gedrückt!");
      speakWeather();
      // Warten bis Taster losgelassen
      while (digitalRead(PIN_PWR_BUTTON) == LOW) {
        delay(10);
      }
      Serial.println("[TTS] Taster losgelassen");
    }
  }
  delay(10);
}
