# TempHumPowv3 - ePaper Display with TTS

ESP32-S3 project for the **Waveshare ESP32-S3-ePaper-1.54G** board.
Extends TempHumPowv2 with speech output via Flite TTS.

## Features

- 200x200 four-color ePaper display (UC8151, black/white/yellow/red)
- Indoor temperature + humidity (SHTC3 sensor)
- Vienna outdoor temperature + humidity (Open-Meteo API)
- Battery percentage with LiPo curve
- WiFi status + time
- **Text-to-Speech** via Flite (English, cmu_us_kal voice)
- Button trigger (GPIO18/BOOT)

## Audio Setup

- **Codec**: ES8311 via I2S + I2C (address 0x18)
- **Amplifier**: GPIO46 (PA_CTRL), GPIO42 (AUDIO_PWR)
- **I2S Pins**: MCLK=14, BCK=15, LRCK=38, DOUT=45
- **Sample rate**: 8kHz, 16-bit, Mono

### Required Libraries (not in Arduino Library Manager)

```bash
cd ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/arduino-audio-tools.git
git clone https://github.com/pschatzmann/arduino-audio-driver.git
git clone https://github.com/pschatzmann/arduino-flite.git
```

## Arduino IDE Settings

- Board: ESP32-S3 Dev Module
- USB CDC On Boot: **Enabled**
- Flash Mode: **QIO** (must be re-set each time Arduino IDE re-detects the board)
- Partition Scheme: Custom (`partitions.csv` - 7MB app + 960KB SPIFFS)

## Upload Procedure

1. Close Serial Monitor
2. Hold **BOOT** + press **RST** (enters download mode)
3. Upload sketch
4. Press **RST** to run

## Pin Mapping

| Function | GPIO |
|----------|------|
| EPD CS | 11 |
| EPD DC | 10 |
| EPD RST | 9 |
| EPD BUSY | 8 |
| EPD MOSI | 13 |
| EPD SCLK | 12 |
| EPD Power | 6 (LOW=on) |
| VBAT Latch | 17 (HIGH=on) |
| Battery ADC | 4 |
| I2C SDA | 47 |
| I2C SCL | 48 |
| I2S MCLK | 14 |
| I2S BCK | 15 |
| I2S LRCK | 38 |
| I2S DOUT | 45 |
| PA Control | 46 (HIGH=on) |
| Audio Power | 42 (LOW=on) |
| Boot Button | 18 |

## Display Layout

```
+----------------------------+----------------------------+
|         ESP32S3/08-2026                               |
+----------------------------+----------------------------+
| INNEN                      | WIEN                       |
| [7-seg temp] oC           | [7-seg temp] oC            |
+----------------------------+----------------------------+
| FEUCHT                     | WIEN                       |
| [7-seg hum] %              | [7-seg hum] %              |
+-------------------------------------------------------+
| [BATT icon] 42%                         WIFI  14:30   |
|                                        TTS:BOOT        |
+-------------------------------------------------------+
```

## Dependencies

- WiFiManager
- HTTPClient
- ArduinoJson
- arduino-flite
- arduino-audio-tools
- arduino-audio-driver
- Wire (I2C)
