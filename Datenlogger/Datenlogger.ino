/* ==========================================================================
   Datenlogger  -  ESP32-S3  1.54" 4-Farben ePaper (G)
   --------------------------------------------------------------------------
   Outdoor Temperatur- & Feuchte-Logger. Geraet bleibt WACH und aktualisiert
   alle 30 min (kein Deep Sleep).

   Pro Zyklus (alle 30 min):
     1. I2C-Bus befreien + durchtakten, SHTC3 lesen (Temp/Feuchte)
     2. Uhrzeit/Datum aus PCF85063-RTC (Register ab 0x04!)
     3. Batterie messen
     4. Messwert an CSV auf SD anhaengen
     5. Ringpuffer aktualisieren
     6. Zwei Graphen zeichnen (beide Panels gelb, Kurven rot)

   UEBERNOMMEN aus deiner Codebase (github.com/MattHum/ESP32-Basics,
   Projekt ESP32-eigenenBilder), unveraendert:
     - src/sd/sdcard_bsp.*        (SD_MMC 1-Bit, setPins(39,41,40))
     - src/display/epaper_driver_bsp.*  (4-Farben-Treiber f. 1.54"G)

   Neu in diesem Projekt: user_config.h, gfx.h, font5x7.h, diese .ino

   HINWEIS POWER: GP17 (VBAT-Latch) wird direkt HIGH gehalten, damit die
   Akku-Versorgung waehrend des Betriebs an bleibt. EPD-Rail GP6 ist LOW-aktiv
   (aus eurem BSP!). Da kein Deep Sleep genutzt wird, entfaellt jedes
   gpio_hold / Pin-Einfrieren - das hatte zuvor den I2C-Bus blockiert.

   RTC EINMALIG STELLEN: unten SET_RTC_FROM_BUILD_TIME aktivieren, flashen,
   1x laufen lassen, wieder deaktivieren, erneut flashen.
   ========================================================================== */

#include <Wire.h>
#include <time.h>
#include "driver/gpio.h"
#include "SD_MMC.h"

#include "user_config.h"
#include "src/display/epaper_driver_bsp.h"
#include "src/sd/sdcard_bsp.h"
#include "gfx.h"

// ------------------------- Konfiguration ----------------------------------
#define UPDATE_MINUTES  30            // Aktualisierungs-Intervall (Geraet bleibt wach)
#define MAX_POINTS      96            // 96 * 30 min = 48 h Historie
#define CSV_PATH        "/datalog.csv"
#define CSV_TAIL_BYTES  8192          // Kaltstart: max. so viel vom Dateiende lesen

// Einmalig zum RTC-Stellen aktivieren, dann wieder auskommentieren:
// #define SET_RTC_FROM_BUILD_TIME

// Farben (4-Farben-G)
#define C_BLACK   0
#define C_WHITE   1
#define C_YELLOW  2
#define C_RED     3

// --------------- Ringpuffer im RTC-Speicher (ueberlebt Deep Sleep) --------
RTC_DATA_ATTR int16_t  rbTemp [MAX_POINTS];   // Zehntelgrad
RTC_DATA_ATTR uint8_t  rbHum  [MAX_POINTS];   // %
RTC_DATA_ATTR uint32_t rbEpoch[MAX_POINTS];   // Unix (UTC0-Basis)
RTC_DATA_ATTR uint16_t rbCount  = 0;
RTC_DATA_ATTR uint16_t rbHead   = 0;
RTC_DATA_ATTR bool     rbLoaded = false;

// ------------------------------ Globals -----------------------------------
float  gTemp = NAN, gHum = NAN;
int    gBatPct = -1;
struct tm gNow;
bool   gTimeValid = false;

epaper_driver_display* epd = nullptr;

// ========================== I2C-Helfer =====================================
static uint8_t bcd2dec(uint8_t b){ return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t dec2bcd(uint8_t d){ return ((d / 10) << 4) | (d % 10); }

// Haengenden I2C-Bus VOR Wire.begin von Hand befreien: haelt ein Device SDA
// nach Reset/Deep-Sleep/Flash (mitten in einer Uebertragung) auf LOW, generieren
// wir bis zu 12 Taktflanken + eine STOP-Bedingung, bis SDA wieder frei ist.
void i2cBusRecover() {
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SCL, HIGH); delayMicroseconds(5);
  for (int i = 0; i < 12 && digitalRead(I2C_SDA) == LOW; i++) {
    digitalWrite(I2C_SCL, LOW);  delayMicroseconds(5);
    digitalWrite(I2C_SCL, HIGH); delayMicroseconds(5);
  }
  // STOP: SDA LOW->HIGH waehrend SCL HIGH
  pinMode(I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SDA, LOW);  delayMicroseconds(5);
  digitalWrite(I2C_SCL, HIGH); delayMicroseconds(5);
  digitalWrite(I2C_SDA, HIGH); delayMicroseconds(5);
}

int scanI2COnce() {
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) found++; }
  return found;
}
// Bus befreien, initialisieren, durchtakten (mit Warten auf die 3V3-Rail)
int primeI2C() {
  gpio_reset_pin((gpio_num_t)I2C_SDA);          // Pin voll zuruecksetzen (loest Deep-Sleep-Hold sicher)
  gpio_reset_pin((gpio_num_t)I2C_SCL);
  pinMode(I2C_SDA, INPUT_PULLUP);
  Serial.printf("[I2C] SDA vor Recovery = %d (0 = Bus haengt)\n", digitalRead(I2C_SDA));
  i2cBusRecover();                              // haengende Leitung befreien
  Wire.begin(I2C_SDA, I2C_SCL); Wire.setClock(100000);
  int found = 0;
  for (int t = 0; t < 8; t++) {                // bis ~1,6 s auf Devices warten
    found = scanI2COnce();
    if (found > 0) break;
    delay(200);
  }
  Serial.printf("[I2C] %d Geraet(e)\n", found);
  return found;
}

// ========================== SHTC3 ==========================================
bool readSensorOnce() {
  Wire.begin(I2C_SDA, I2C_SCL); Wire.setClock(100000);
  Wire.beginTransmission(SHTC3_ADDR); Wire.write(0x35); Wire.write(0x17);   // Wakeup
  if (Wire.endTransmission() != 0) return false;
  delay(1);
  Wire.beginTransmission(SHTC3_ADDR); Wire.write(0x78); Wire.write(0x66);   // Messung, T zuerst
  if (Wire.endTransmission() != 0) return false;
  delay(20);
  if (Wire.requestFrom((uint8_t)SHTC3_ADDR, (uint8_t)6) != 6) return false;
  uint16_t rawT = (Wire.read() << 8) | Wire.read(); Wire.read();
  uint16_t rawH = (Wire.read() << 8) | Wire.read(); Wire.read();
  gTemp = -45.0f + 175.0f * ((float)rawT / 65535.0f);
  gHum  = 100.0f * ((float)rawH / 65535.0f);
  if (gHum < 0) gHum = 0; if (gHum > 100) gHum = 100;
  Wire.beginTransmission(SHTC3_ADDR); Wire.write(0xB0); Wire.write(0x98);   // Sleep
  Wire.endTransmission();
  return true;
}
void readSensor() {
  for (int i = 0; i < 3; i++) { if (readSensorOnce()) { Serial.printf("[SHTC3] %.1fC %.0f%%\n", gTemp, gHum); return; } delay(30); }
  Serial.println("[SHTC3] Lesefehler");
}

// ========================== Batterie =======================================
static int lipoPercent(float v) {
  const float vt[] = {3.30,3.50,3.60,3.70,3.75,3.80,3.85,3.90,3.95,4.00,4.10,4.20};
  const int   pt[] = {   0,   5,  10,  25,  40,  55,  65,  75,  85,  90,  96, 100};
  const int n = 12;
  if (v <= vt[0]) return 0; if (v >= vt[n-1]) return 100;
  for (int i = 1; i < n; i++) if (v < vt[i])
    return (int)(pt[i-1] + (v-vt[i-1])/(vt[i]-vt[i-1])*(pt[i]-pt[i-1]) + 0.5f);
  return 100;
}
void readBattery() {
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  uint32_t sum = 0; for (int i = 0; i < 16; i++) { sum += analogReadMilliVolts(BATTERY_ADC_PIN); delay(2); }
  float vBat = (sum / 16.0f) / 1000.0f * 2.0f;
  gBatPct = lipoPercent(vBat);
  Serial.printf("[BAT] %.2fV -> %d%%\n", vBat, gBatPct);
}

// ========================== PCF85063ATL RTC ================================
bool readTimeFromRTC() {
  Wire.begin(I2C_SDA, I2C_SCL); Wire.setClock(100000);
  Wire.beginTransmission(RTC_I2C_ADDR); Wire.write(0x04);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((uint8_t)RTC_I2C_ADDR, (uint8_t)7) != 7) return false;
  uint8_t sec = Wire.read(), mins = Wire.read(), hrs = Wire.read();
  uint8_t day = Wire.read(), wday = Wire.read(), mon = Wire.read(), yr = Wire.read(); (void)wday;
  if (sec & 0x80) return false;                       // OS-Bit: ungueltig
  gNow.tm_sec  = bcd2dec(sec & 0x7F);
  gNow.tm_min  = bcd2dec(mins & 0x7F);
  gNow.tm_hour = bcd2dec(hrs & 0x3F);
  gNow.tm_mday = bcd2dec(day & 0x3F);
  gNow.tm_mon  = bcd2dec(mon & 0x1F) - 1;
  gNow.tm_year = bcd2dec(yr) + 100;
  gNow.tm_isdst = 0;
  return true;
}
void writeTimeToRTC(const struct tm &t) {
  Wire.begin(I2C_SDA, I2C_SCL); Wire.setClock(100000);
  Wire.beginTransmission(RTC_I2C_ADDR); Wire.write(0x04);
  Wire.write(dec2bcd(t.tm_sec) & 0x7F);
  Wire.write(dec2bcd(t.tm_min));  Wire.write(dec2bcd(t.tm_hour));
  Wire.write(dec2bcd(t.tm_mday)); Wire.write(dec2bcd(t.tm_wday == 0 ? 7 : t.tm_wday));
  Wire.write(dec2bcd(t.tm_mon + 1)); Wire.write(dec2bcd((t.tm_year + 1900) % 100));
  Wire.endTransmission();
}
#ifdef SET_RTC_FROM_BUILD_TIME
void setRtcFromBuildTime() {
  char mon[4]; int d, y, hh, mm, ss;
  const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  sscanf(__DATE__, "%3s %d %d", mon, &d, &y);
  sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
  int mi = (int)((strstr(months, mon) - months) / 3);
  struct tm t = {}; t.tm_year = y - 1900; t.tm_mon = mi; t.tm_mday = d;
  t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss;
  time_t e = mktime(&t); struct tm w; localtime_r(&e, &w);
  writeTimeToRTC(w);
  Serial.printf("[RTC] gestellt: %04d-%02d-%02d %02d:%02d:%02d\n", y, mi+1, d, hh, mm, ss);
}
#endif

// ========================== SD: CSV ========================================
// sdcard_init()/sdcard_fileExists() stammen aus dem uebernommenen BSP.
void sdAppendRow(uint32_t epoch) {
  bool newFile = !sdcard_fileExists(CSV_PATH);
  File f = SD_MMC.open(CSV_PATH, FILE_APPEND);
  if (!f) { Serial.println("[SD] open(APPEND) fehlgeschlagen"); return; }
  if (newFile) f.println("epoch,datetime,temp_C,hum_pct,batt_pct");
  char dt[24] = "0000-00-00 00:00:00";
  if (gTimeValid)
    snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d",
             gNow.tm_year + 1900, gNow.tm_mon + 1, gNow.tm_mday, gNow.tm_hour, gNow.tm_min, gNow.tm_sec);
  char line[80];
  snprintf(line, sizeof(line), "%lu,%s,%.1f,%.0f,%d", (unsigned long)epoch, dt, gTemp, gHum, gBatPct);
  f.println(line); f.close();
  Serial.printf("[SD] + %s\n", line);
}
void sdLoadTail() {
  if (!sdcard_fileExists(CSV_PATH)) return;
  File f = SD_MMC.open(CSV_PATH, FILE_READ);
  if (!f) return;
  size_t sz = f.size();
  size_t start = (sz > CSV_TAIL_BYTES) ? sz - CSV_TAIL_BYTES : 0;
  f.seek(start);
  if (start > 0) f.readStringUntil('\n');            // angeschnittene Zeile verwerfen
  while (f.available()) {
    String ln = f.readStringUntil('\n'); ln.trim();
    if (ln.length() == 0 || ln.startsWith("epoch")) continue;
    unsigned long ep = 0; float t = 0; int h = 0;
    if (sscanf(ln.c_str(), "%lu,%*[^,],%f,%d", &ep, &t, &h) == 3) {
      rbEpoch[rbHead] = (uint32_t)ep;
      rbTemp [rbHead] = (int16_t)lroundf(t * 10.0f);
      rbHum  [rbHead] = (uint8_t)constrain(h, 0, 100);
      rbHead = (rbHead + 1) % MAX_POINTS;
      if (rbCount < MAX_POINTS) rbCount++;
    }
  }
  f.close();
  Serial.printf("[SD] Tail geladen: %d Punkte\n", rbCount);
}

void ringPush(uint32_t epoch) {
  rbEpoch[rbHead] = epoch;
  rbTemp [rbHead] = (int16_t)lroundf(gTemp * 10.0f);
  rbHum  [rbHead] = (uint8_t)lroundf(gHum);
  rbHead = (rbHead + 1) % MAX_POINTS;
  if (rbCount < MAX_POINTS) rbCount++;
}

// ========================== Zeichnen =======================================
// Mitlaufende Zeitachse: die letzten 6 h bis "jetzt", 30-min-Raster.
#define AXIS_HOURS   6
#define AXIS_TICKS   (AXIS_HOURS * 2)          // 30-min-Ticks -> 12 Intervalle, 13 Marken
#define AXIS_SPAN    (AXIS_HOURS * 3600UL)

static uint32_t gWinStart = 0;                 // linker Achsenrand (Unix, auf :00/:30 gerundet)

// Alle Messpunkte im aktuellen 6h-Fenster, chronologisch
static float    wvT[64], wvH[64];
static uint32_t wvE[64];
static int      wN = 0;

void buildWindow() {
  wN = 0;
  if (rbCount == 0) { gWinStart = 0; return; }
  uint32_t nowE = rbEpoch[(rbHead - 1 + MAX_POINTS) % MAX_POINTS];
  uint32_t tEnd = ((nowE + 1799UL) / 1800UL) * 1800UL;      // rechter Rand: auf naechste 30 min aufrunden
  gWinStart = (tEnd >= AXIS_SPAN) ? (tEnd - AXIS_SPAN) : 0; // Fenster laeuft mit
  uint32_t t1 = gWinStart + AXIS_SPAN;
  int idx = (rbHead - rbCount + MAX_POINTS) % MAX_POINTS;
  for (int i = 0; i < rbCount; i++) {
    uint32_t e = rbEpoch[idx];
    if (e >= gWinStart && e <= t1 && wN < 64) {
      wvT[wN] = rbTemp[idx] / 10.0f; wvH[wN] = rbHum[idx]; wvE[wN] = e; wN++;
    }
    idx = (idx + 1) % MAX_POINTS;
  }
}

void drawGraph(Gfx& g, int bx0, int by0, int bx1, int by1, uint8_t bg,
               const char* title, const char* curStr,
               const float* vals, const uint32_t* eps, int n,
               float vmin, float vmax, bool asInt) {
  g.fillRect(bx0, by0, bx1, by1, bg);
  g.rect(bx0, by0, bx1, by1, C_BLACK);

  int px0 = bx0 + 18, px1 = bx1 - 4;
  int py0 = by0 + 15, py1 = by1 - 34;          // unten Platz fuer vertikale Zeitlabels

  g.text(bx0 + 3, by0 + 3, title, C_BLACK, 1);
  g.textR(bx1 - 3, by0 + 3, curStr, C_BLACK, 2);

  g.vLine(px0, py0, py1, C_BLACK);
  g.hLine(px0, px1, py1, C_BLACK);
  char lab[8];
  snprintf(lab, sizeof(lab), "%d", (int)lroundf(vmax)); g.text(bx0 + 1, py0 - 3, lab, C_BLACK, 1);
  snprintf(lab, sizeof(lab), "%d", (int)lroundf(vmin)); g.text(bx0 + 1, py1 - 6, lab, C_BLACK, 1);

  // 30-min-Ticks + vertikale Uhrzeit-Labels (z.B. 16:30, 17:00, ... jetzt)
  for (int k = 0; k <= AXIS_TICKS; k++) {
    int gx = px0 + (px1 - px0) * k / AXIS_TICKS;
    g.vLine(gx, py1, py1 + 2, C_BLACK);
    if (gTimeValid) {
      char t[6]; time_t e = gWinStart + (uint32_t)k * 1800UL; struct tm tmv;
      localtime_r(&e, &tmv);
      snprintf(t, sizeof(t), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
      g.textV(gx - 3, py1 + 4, t, C_BLACK);
    }
  }

  if (n < 1) return;
  float span = vmax - vmin; if (span < 0.5f) span = 0.5f;

  // Punkt-Positionen (zeitproportional auf der festen Achse)
  static int xs[64], ys[64];
  for (int i = 0; i < n; i++) {
    uint32_t off = (eps[i] > gWinStart) ? (eps[i] - gWinStart) : 0;
    if (off > AXIS_SPAN) off = AXIS_SPAN;
    xs[i] = px0 + (int)((uint64_t)(px1 - px0) * off / AXIS_SPAN);
    float fv = (vals[i] - vmin) / span; if (fv < 0) fv = 0; if (fv > 1) fv = 1;
    ys[i] = py1 - (int)((py1 - py0) * fv);
  }
  // Verbindungslinie (rot)
  for (int i = 1; i < n; i++) g.line(xs[i-1], ys[i-1], xs[i], ys[i], C_RED, 2);
  // Marker + Wert oberhalb (mit Kollisionsschutz)
  int lastR = -999;
  for (int i = 0; i < n; i++) {
    g.fillRect(xs[i] - 1, ys[i] - 1, xs[i] + 1, ys[i] + 1, C_RED);
    char v[8];
    if (asInt) snprintf(v, sizeof(v), "%d", (int)lroundf(vals[i]));
    else       snprintf(v, sizeof(v), "%.1f", vals[i]);
    int vw = g.textW(v, 1), vx = xs[i] - vw / 2;
    if (vx < px0) vx = px0; if (vx + vw > bx1 - 2) vx = bx1 - 2 - vw;
    if (vx > lastR + 1) {                        // nur zeichnen, wenn kein Ueberlapp
      int vy = ys[i] - 9; if (vy < by0 + 13) vy = by0 + 13;
      g.text(vx, vy, v, C_BLACK, 1);
      lastR = vx + vw;
    }
  }
}

void drawUI(Gfx& g) {
  buildWindow();

  float tMin = 999, tMax = -999, hMin = 100, hMax = 0;
  for (int i = 0; i < wN; i++) {
    if (wvT[i] < tMin) tMin = wvT[i]; if (wvT[i] > tMax) tMax = wvT[i];
    if (wvH[i] < hMin) hMin = wvH[i]; if (wvH[i] > hMax) hMax = wvH[i];
  }
  if (wN == 0) { tMin = 0; tMax = 30; hMin = 0; hMax = 100; }
  tMin = floorf(tMin) - 1; tMax = ceilf(tMax) + 1; if (tMax - tMin < 4) tMax = tMin + 4;
  hMin = floorf(hMin) - 5; if (hMin < 0) hMin = 0;
  hMax = ceilf(hMax) + 5;  if (hMax > 100) hMax = 100;
  if (hMax - hMin < 10) hMax = (hMin + 10 > 100) ? 100 : hMin + 10;

  epd->EPD_Clear();

  // Titelzeile: Titel links, Uhrzeit + Akku rechts (zweizeilig)
  g.text(4, 3, "Datenlogger", C_BLACK, 2);
  if (gTimeValid) {
    char clk[8]; snprintf(clk, sizeof(clk), "%02d:%02d", gNow.tm_hour, gNow.tm_min);
    g.textR(198, 2, clk, C_BLACK, 1);
  }
  if (gBatPct >= 0) { char b[8]; snprintf(b, sizeof(b), "%d%%", gBatPct); g.textR(198, 11, b, C_BLACK, 1); }
  g.hLine(0, 199, 20, C_BLACK);

  // aktuelle Werte (letzter Messwert) gross im Panel-Kopf
  char curT[12], curH[12];
  if (wN > 0) { snprintf(curT, sizeof(curT), "%.1f", wvT[wN-1]); snprintf(curH, sizeof(curH), "%.0f%%", wvH[wN-1]); }
  else { snprintf(curT, sizeof(curT), "--"); snprintf(curH, sizeof(curH), "--"); }

  // Beide Panels gelb, Kurve rot, feste 6h-Zeitachse (30-min-Raster)
  drawGraph(g, 0, 24, 199, 110,  C_YELLOW, "Temp C",       curT, wvT, wvE, wN, tMin, tMax, false);
  drawGraph(g, 0, 112, 199, 199, C_YELLOW, "rel. Feuchte", curH, wvH, wvE, wN, hMin, hMax, true);

  epd->EPD_Display();
}

// ========================== Mess-Zyklus ====================================
void measureAndDraw(bool coldBoot) {
  Serial.println("--- Messung ---");
  // Sensorik (Bus befreien + durchtakten, dann lesen)
  primeI2C();
  readSensor();
  readBattery();
  gTimeValid = readTimeFromRTC();
  Serial.printf("[ZEIT] %s\n", gTimeValid ? "gueltig" : "UNGUELTIG (RTC stellen!)");

  uint32_t epoch;
  if (gTimeValid) { struct tm t = gNow; t.tm_isdst = 0; epoch = (uint32_t)mktime(&t); }
  else epoch = (rbCount > 0) ? rbEpoch[(rbHead - 1 + MAX_POINTS) % MAX_POINTS] + UPDATE_MINUTES * 60UL : 0;

  // SD (uebernommenes BSP)
  if (sdcard_init()) {
    if (coldBoot && !rbLoaded) { sdLoadTail(); rbLoaded = true; }
    sdAppendRow(epoch);
    SD_MMC.end();
  } else {
    Serial.println("[SD] nicht verfuegbar - nur Anzeige aus RTC-Puffer");
  }

  // Ringpuffer + Anzeige
  ringPush(epoch);
  epd->EPD_Init();                 // Panel vor jedem Vollrefresh initialisieren
  Gfx g(epd);
  drawUI(g);
}

// ========================== setup ==========================================
void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("\n=== Datenlogger Start ===");
  setenv("TZ", "UTC0", 1); tzset();                  // stabile mktime/localtime

  // Versorgung an (bleibt an - kein Deep Sleep mehr)
  pinMode(VBAT_PWR_PIN,  OUTPUT); digitalWrite(VBAT_PWR_PIN,  HIGH);  // Akku-Latch halten
  // WICHTIG: Audio-Rail AN (GP42 LOW). Der ES8311 haengt am selben I2C-Bus;
  // unversorgt zieht er ueber seine ESD-Dioden SDA auf LOW und blockiert den
  // ganzen Bus (SDA=0, 0 Geraete). Waveshare schaltet das offiziell vor I2C ein.
  pinMode(AUDIO_PWR_PIN, OUTPUT); digitalWrite(AUDIO_PWR_PIN, LOW);
  pinMode(EPD_PWR_PIN,   OUTPUT); digitalWrite(EPD_PWR_PIN,   LOW); delay(50);  // GP6 LOW = EPD an
  pinMode(BTN_PWR_PIN,   INPUT_PULLUP);                              // Taster (aktiv LOW) fuer manuelles Update

#ifdef SET_RTC_FROM_BUILD_TIME
  primeI2C();                      // Bus fuer das RTC-Stellen initialisieren
  setRtcFromBuildTime();
#endif

  // Display einmalig anlegen (SPI-Bus-Init sitzt im Konstruktor -> nur 1x!)
  custom_lcd_spi_t spi_cfg = {
    .cs = EPD_PIN_CS, .dc = EPD_PIN_DC, .rst = EPD_PIN_RST, .busy = EPD_PIN_BUSY,
    .mosi = EPD_PIN_MOSI, .scl = EPD_PIN_SCK, .spi_host = EPD_SPI_HOST, .buffer_len = EPD_BUFFER_LEN
  };
  epd = new epaper_driver_display(EPD_WIDTH, EPD_HEIGHT, spi_cfg);

  // Erste Messung + Anzeige sofort
  measureAndDraw(true);
}

// ========================== loop: alle 30 min oder auf Tastendruck =========
void loop() {
  const uint32_t intervalMs = (uint32_t)UPDATE_MINUTES * 60UL * 1000UL;
  uint32_t waited = 0;
  bool manual = false;

  // Warten bis Intervall abgelaufen ODER Taster gedrueckt (Watchdog-freundlich)
  while (waited < intervalMs) {
    if (digitalRead(BTN_PWR_PIN) == LOW) {           // Taster aktiv LOW
      delay(30);                                     // entprellen
      if (digitalRead(BTN_PWR_PIN) == LOW) {
        manual = true;
        while (digitalRead(BTN_PWR_PIN) == LOW) delay(10);   // auf Loslassen warten
        break;
      }
    }
    delay(50); waited += 50;
  }

  if (manual) Serial.println("[BTN] manuelles Update ausgeloest");
  measureAndDraw(false);
}
