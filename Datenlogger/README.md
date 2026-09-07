# Datenlogger — ESP32-S3 1.54" 4-Farben ePaper (G)

Outdoor Temperatur-/Feuchte-Logger. Wacht alle 30 min auf, misst Temperatur
und Feuchte (SHTC3), liest die Uhrzeit (PCF85063-RTC), schreibt eine Zeile in
`/datalog.csv` auf die SD-Karte, zeichnet zwei Graphen (Temperatur = roter
Hintergrund, Feuchte = gelber Hintergrund) mit Zeitachse und geht wieder in
Deep Sleep. Titel: **Datenlogger**.

## Dateien

| Datei | Herkunft |
|-------|----------|
| `Datenlogger.ino` | neu — Hauptlogik, Messung, CSV, Graphen, Deep Sleep |
| `user_config.h` | neu — Pin-Belegung dieses Boards |
| `gfx.h` | neu — Linien/Rechteck/Text auf dem Pixel-Treiber |
| `font5x7.h` | neu — kompakter 5×7-Font |
| `src/sd/sdcard_bsp.*` | **übernommen** aus deinem Repo (SD_MMC 1-Bit) |
| `src/display/epaper_driver_bsp.*` | **übernommen** — 4-Farben-Treiber 1.54"G |
| `src/power/board_power_bsp.*` | **übernommen** — beigelegt (s. Power-Hinweis) |

Quelle der übernommenen Dateien: `github.com/MattHum/ESP32-Basics`
(Projekt `ESP32-eigenenBilder`), unverändert übernommen.

## Wichtig: SD-Karte = SD_MMC 1-Bit (kein SPI!)

Auf diesem Board ist die SD-Karte **nicht** als SPI verdrahtet, sondern für
SD_MMC 1-Bit: `CLK=GP39`, `CMD=GP41` (SD_MOSI-Netz), `D0=GP40` (SD_MISO-Netz).
Eine CS-Leitung existiert nicht (R41 unbestückt). Dein `sdcard_bsp` macht genau
das Richtige: `SD_MMC.setPins(39,41,40)` + `SD_MMC.begin("/sdcard", true)`.
→ Braucht **Arduino-ESP32 Core ≥ 2.0.5** (wegen `setPins`).

## Wichtig: EPD-Rail GP6 ist LOW-aktiv

Aus deinem `board_power_bsp`: `POWEER_EPD_ON()` treibt GP6 auf **LOW**. Der
Sketch schaltet das Display darum mit `GP6 = LOW` ein und `GP6 = HIGH` aus.
(Das widerspricht der Pinout-Doku, die HIGH=an angibt — die laufende Firmware
gilt.)

## Deep-Sleep-Sicherheit (Akkubetrieb)

Der VBAT-Latch GP17 wird **direkt** im Sketch gesteuert, nicht über
`board_power_bsp`: dessen Konstruktor würde GP17 kurz auf LOW ziehen und im
Akkubetrieb das Board abschalten. Beim Aufwachen wird GP17 sofort HIGH getrieben
und über den Sleep per `gpio_hold` gehalten.

## Setup

1. **Board:** „ESP32S3 Dev Module", PSRAM aktiviert, passende Flash-Größe (N8R8).
2. **Bibliotheken:** nur Bordmittel des ESP32-Cores (`Wire`, `SD_MMC`, ESP-IDF).
   Keine externen Libs nötig.
3. **RTC einmalig stellen:** in `Datenlogger.ino` `#define SET_RTC_FROM_BUILD_TIME`
   aktivieren, flashen, einmal laufen lassen, wieder auskommentieren, erneut
   flashen. (Alternativ per WLAN/NTP — Baustein im Board-Skill.)
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
- `[ZEIT] gueltig` → RTC gestellt. Sonst x-Achse fällt auf 30-min-Raster zurück.

## Parameter

- Intervall: `SLEEP_MINUTES` (Standard 30)
- Historie im Graph: `MAX_POINTS` (Standard 96 = 48 h). Überlebt Deep Sleep im
  RTC-Speicher; nach Stromausfall wird der Verlauf aus dem CSV-Ende nachgeladen.
