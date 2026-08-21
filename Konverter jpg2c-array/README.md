# jpg2c — ePaper 4-Farben C-Array Konverter

Browser-basiertes Tool zur Konvertierung von JPG/PNG-Bildern in C-Arrays für das **Waveshare ESP32-S3-ePaper-1.54G** Display.

## Display

| Eigenschaft | Wert |
|---|---|
| Board | Waveshare ESP32-S3-ePaper-1.54G |
| Controller | UC8151 |
| Auflösung | 200 x 200 Pixel |
| Farben | Schwarz, Weiss, Gelb, Rot (4 Farben, 2 bpp) |
| Buffer | 10.000 Byte pro Bild |

## Farbcodes

| Code | Farbe | Bitmuster |
|---|---|---|
| 0 | Schwarz | `00` |
| 1 | Weiss | `01` |
| 2 | Gelb | `10` |
| 3 | Rot | `11` |

## Pixel-Layout

- **2 Bit pro Pixel**, 4 Pixel pro Byte, MSB-first
- **Byte-Index**: `Y * 50 + X / 4`
- **Bit-Shift**: `(3 - (X % 4)) * 2`

## Funktion

1. JPG oder PNG per Drag & Drop oder Dateiauswahl laden
2. Bilder werden automatisch als Center-Crop auf 200x200 skaliert
3. Vorschau: Original und 4-Farben-Konvertierung side-by-side
4. Optional: ASCII-Vorschau (4x skaliert)
5. Export als `.h` + `.cpp` C-Array-Dateien

## Farbzuordnung (RGB-Schwellwerte)

| Farbe | Regel |
|---|---|
| Schwarz | R < 80 und G < 80 und B < 80 |
| Weiss | R > 200 und G > 200 und B > 200 |
| Rot | R > 180 und G < 100 und B < 100 |
| Gelb | R > 180 und G > 180 und B < 100 |
| Sonstiges | Hintergrundfarbe (konfigurierbar) |

## Verwendung im Arduino-Projekt

```cpp
#include "mein_bild.h"

const unsigned char *img = mein_bild;
memcpy(epd->getBuffer(), img, 10000);
epd->EPD_Display();
```

## Technische Hinweise

- Laeuft vollstaendig lokal im Browser — keine Server-Kommunikation
- Konvertierte Bilder koennen direkt als Arduino-kompilierbare C-Arrays exportiert werden
- Mehrere Bilder gleichzeitig verarbeiten und einzeln oder kombiniert herunterladen
