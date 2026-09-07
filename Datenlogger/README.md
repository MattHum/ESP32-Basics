# Datenlogger — ESP32-S3 1.54" 4-Farben ePaper (G)

Outdoor Temperatur-/Feuchte-Logger. Das Gerät bleibt **wach** und aktualisiert
alle 30 min (kein Deep Sleep). Pro Zyklus wird Temperatur und Feuchte (SHTC3)
gemessen, die Uhrzeit vom PCF85063-RTC gelesen, eine Zeile an `/datalog.csv`
auf der SD-Karte angehängt und zwei Graphen gezeichnet (steigende Zeitachse,
30-min-Raster, beide Panels gelb, Kurven rot). Titel: **Datenlogger**.

## Ablauf pro Messzyklus

1. I2C-Bus befreien + durchtakten (SHTC3 + RTC hängen am selben Bus wie der
   ES8311-Codec; ohne Versorgung zieht der Codec über ESD-Dioden SDA auf LOW)
2. SHTC3 lesen (Temp/Feuchte)
3. Uhrzeit/Datum aus PCF85063-RTC (Register ab 0x04!)
4. Batterie messen (LiPo-Kennlinie, 16-fach gemittelt)
5. Messwert an CSV auf SD anhängen
6. Ringpuffer aktualisieren (96 Punkte = 48 h Historie)
7. Zwei Graphen zeichnen (Temperatur + relative Feuchte)

Zusätzlich per **Taster (GPIO18, aktiv LOW)** kann jederzeit ein manuelles
Update ausgelöst werden.

## Dateien

| Datei | Herkunft |
|-------|----------|
| `Datenlogger.ino` | neu — Hauptlogik, Messung, CSV, Graphen, Wach-Betrieb |
| `user_config.h` | neu — Pin-Belegung dieses Boards |
| `gfx.h` | neu — Linien/Rechteck/Text auf dem Pixel-Treiber |
| `font5x7.h` | neu — kompakter 5×7-Font |
| `src/sd/sdcard_bsp.*` | **übernommen** aus deinem Repo (SD_MMC 1-Bit) |
| `src/display/epaper_driver_bsp.*` | **übernommen** — 4-Farben-Treiber 1.54"G |

Quelle der übernommenen Dateien: `github.com/MattHum/ESP32-Basics`
(Projekt `ESP32-eigenenBilder`), unverändert übernommen.

## Wichtig: SD-Karte = SD_MMC 1-Bit (kein SPI!)

Auf diesem Board ist die SD-Karte **nicht** als SPI verdrahtet, sondern für
SD_MMC 1-Bit: `CLK=GP39`, `CMD=GP41` (SD_MOSI-Netz), `D0=GP40` (SD_MISO-Netz).
Eine CS-Leitung existiert nicht (R41 unbestückt). Das `sdcard_bsp` macht genau
das Richtige: `SD_MMC.setPins(39,41,40)` + `SD_MMC.begin("/sdcard", true)`.
→ Braucht **Arduino-ESP32 Core ≥ 2.0.5** (wegen `setPins`).

## Wichtig: EPD-Rail GP6 ist LOW-aktiv

Aus dem übernommenen `board_power_bsp`: die EPD-Rail wird mit **GP6 = LOW**
eingeschaltet. Der Sketch schaltet das Display darum mit `GP6 = LOW` an.
(Das widerspricht der Pinout-Doku, die HIGH=an angibt — die laufende Firmware
gilt.)

## Power-Betrieb

- **VBAT-Latch GP17:** wird direkt im Sketch auf HIGH gehalten, damit die
  Akku-Versorgung während des laufenden Betriebs an bleibt.
- **Audio-Rail GP42:** wird auf LOW gesetzt (Rail AN). Der ES8311-Codec hängt am
  selben I2C-Bus; unversorgt zieht er über seine ESD-Dioden SDA auf LOW und
  blockiert den gesamten Bus (SDA=0, 0 Geräte gefunden). Waveshare schaltet die
  Audio-Rail offiziell **vor** der I2C-Initialisierung ein.
- Da kein Deep Sleep genutzt wird, entfällt jedes `gpio_hold`/Pin-Einfrieren.

## Setup

1. **Board:** „ESP32S3 Dev Module", PSRAM aktiviert, passende Flash-Größe (N8R8).
2. **Bibliotheken:** nur Bordmittel des ESP32-Cores (`Wire`, `SD_MMC`, ESP-IDF).
   Keine externen Libs nötig.
3. **RTC einmalig stellen:** in `Datenlogger.ino` `#define SET_RTC_FROM_BUILD_TIME`
   aktivieren, flashen, einmal laufen lassen, wieder auskommentieren, erneut
   flashen.
4. **SD-Karte** FAT32 formatiert einlegen. Erste Zeile der CSV wird automatisch
   als Header geschrieben.

## CSV-Format

```
epoch,datetime,temp_C,hum_pct,batt_pct
1700000000,2023-11-14 22:13:20,12.3,64,87
```

## Vor dem Feldeinsatz am Serial-Log prüfen (115200 Baud)

- `[SD] Mounted: … MB` → Karte ok. Sonst `setPins/mount FAILED` → Karte/Format.
- `[SHTC3] 12.3C 64%` → Sensor ok.
- `[ZEIT] gueltig` → RTC gestellt. Sonst fällt die x-Achse auf ein 30-min-Raster
  zurück.

## Parameter

- Intervall: `UPDATE_MINUTES` (Standard 30)
- Historie im Graph: `MAX_POINTS` (Standard 96 = 48 h). Nach Kaltstart wird der
  Verlauf aus dem CSV-Ende nachgeladen (`CSV_TAIL_BYTES` 8192).
- Zeitachse: `AXIS_HOURS` (Standard 6 h, mitlaufend, 30-min-Raster)