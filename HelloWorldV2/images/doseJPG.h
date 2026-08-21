#ifndef JPG2C_DOSEJPG_H
#define JPG2C_DOSEJPG_H

#include <stdint.h>

// Auto-generiert mit jpg2c.html
// Display: Waveshare ESP32-S3-ePaper-1.54G, 200x200px, 4 Farben (2bpp)
// Layout: [Y*50 + X/4], MSB-first, 4 Pixel/Byte

#define JPG2C_WIDTH  200
#define JPG2C_HEIGHT 200
#define JPG2C_BUFSIZE 10000

// Farbcodes: 0=Schwarz 1=Weiss 2=Gelb 3=Rot

extern const uint8_t doseJPG[JPG2C_BUFSIZE];

#endif // JPG2C_DOSEJPG_H
