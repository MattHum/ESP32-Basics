# ESP32-eigenen Bilder - Image Gallery with Audio

Image slideshow for the **Waveshare ESP32-S3-ePaper-1.54G** board.
Displays images on the four-color ePaper screen with optional audio playback from SD card.

## Features

- 200x200 four-color ePaper display (UC8151, black/white/yellow/red)
- 3 built-in images (compiled into flash): Dose, Kopf, Muecke
- Button-triggered image cycling (BOOT button)
- Audio playback from SD card (MP3) via ES8311 codec
- Audio files matched to images: Kopf + Muecke play sound, Dose is silent

## Hardware

- **Display**: Waveshare 1.54G four-color ePaper (200x200)
- **Codec**: ES8311 via I2S + I2C (custom driver, not Arduino audio libraries)
- **Amplifier**: GPIO46 (PA_CTRL), GPIO42 (AUDIO_PWR)
- **SD Card**: Native SPI via HSPI
- **Button**: GPIO0 (BOOT) - next image + play audio

## Audio Setup

Audio files must be placed on the SD card as MP3:
- Image 0 (Dose): no audio
- Image 1 (Kopf): plays audio from SD
- Image 2 (Muecke): plays audio from SD

File paths are defined in `src/images.cpp`. I2S runs at 24kHz, 256x MCLK.

## Arduino IDE Settings

- Board: ESP32-S3 Dev Module
- USB CDC On Boot: **Enabled**
- Flash Mode: **QIO** (must be re-set each time Arduino IDE re-detects the board)

## Upload Procedure

1. Close Serial Monitor
2. Hold **BOOT** + press **RST** (enters download mode)
3. Upload sketch
4. Press **RST** to run

## Adding Custom Images

1. Convert your image to a 200x200, 2-bit (4-color) raw buffer using `generate_images.py`
2. Place the output in `images/` and `src/`
3. Register in `images/images.h` and `src/images.cpp`

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
| I2S MCLK | 14 |
| I2S BCK | 15 |
| I2S LRCK | 38 |
| I2S DOUT | 45 |
| I2S DIN | 16 |
| PA Control | 46 (HIGH=on) |
| Audio Power | 42 (LOW=on) |
| I2C SDA | 47 |
| I2C SCL | 48 |
| Boot Button | 0 |
| PWR Button | 18 |

## Project Structure

```
ESP32-eigenen Bilder/
  ESP32-eigenenBilder.ino   Main sketch
  user_config.h             Pin definitions & constants
  generate_images.py        Python tool to convert images to 2-bit buffers
  view_image.py             Python tool to preview raw buffers
  images/
    images.h                Image registry (count, arrays, audio paths)
    doseJPG.h               Dose image data
    KopfJPG.h               Kopf image data
    mueckeJPG.h             Muecke image data
  src/
    images.cpp              Image + audio path arrays
    doseJPG.cpp             Dose image buffer
    KopfJPG.cpp             Kopf image buffer
    mueckeJPG.cpp           Muecke image buffer
    audio/
      audio_player.h/cpp    AudioPlayer class (I2S + ES8311)
      es8311.h/cpp          Custom ES8311 codec driver
      es8311_reg.h          ES8311 register definitions
    display/
      epaper_driver_bsp.h/cpp   ePaper SPI driver
    power/
      board_power_bsp.h/cpp     Power management (EPD, Audio, Battery)
    sd/
      sdcard_bsp.h/cpp          SD card initialization
```

## Dependencies

- ESP32 Arduino Core (includes `Audio.h` for I2S)
- Wire (I2C)
- No external Arduino libraries required (all drivers are custom, in `src/`)
