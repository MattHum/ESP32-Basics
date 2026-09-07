/* Kleine Grafik-Ebene auf dem 4-Farben-Treiber (epaper_driver_display).
   Zeichnet direkt via EPD_DrawColorPixel(). Farben: 0=Schwarz 1=Weiss 2=Gelb 3=Rot */
#ifndef GFX_H
#define GFX_H

#include <Arduino.h>
#include "font5x7.h"
#include "src/display/epaper_driver_bsp.h"

class Gfx {
public:
  Gfx(epaper_driver_display* epd) : _epd(epd) {}

  void pixel(int x, int y, uint8_t c) {
    if (x < 0 || y < 0 || x >= 200 || y >= 200) return;
    _epd->EPD_DrawColorPixel((uint16_t)x, (uint16_t)y, c);
  }

  void hLine(int x0, int x1, int y, uint8_t c) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; x++) pixel(x, y, c);
  }
  void vLine(int x, int y0, int y1, uint8_t c) {
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) pixel(x, y, c);
  }

  void line(int x0, int y0, int x1, int y1, uint8_t c, int thick = 1) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
      if (thick <= 1) pixel(x0, y0, c);
      else for (int a = 0; a < thick; a++) for (int b = 0; b < thick; b++) pixel(x0 + a, y0 + b, c);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  void fillRect(int x0, int y0, int x1, int y1, uint8_t c) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) pixel(x, y, c);
  }
  void rect(int x0, int y0, int x1, int y1, uint8_t c) {
    hLine(x0, x1, y0, c); hLine(x0, x1, y1, c);
    vLine(x0, y0, y1, c); vLine(x1, y0, y1, c);
  }

  // Ein Zeichen, Skalierung s (Pixelgroesse). Breite = 6*s (5 + 1 Luecke).
  void ch(int x, int y, char ci, uint8_t fg, int s = 1) {
    if (ci < 32 || ci > 126) ci = '?';
    const uint8_t* g = FONT5X7[ci - 32];
    for (int col = 0; col < 5; col++) {
      uint8_t bits = pgm_read_byte(&g[col]);
      for (int row = 0; row < 7; row++) {
        if (bits & (1 << row)) {
          if (s == 1) pixel(x + col, y + row, fg);
          else fillRect(x + col * s, y + row * s, x + col * s + s - 1, y + row * s + s - 1, fg);
        }
      }
    }
  }

  int textW(const char* str, int s = 1) { return (int)strlen(str) * 6 * s; }

  void text(int x, int y, const char* str, uint8_t fg, int s = 1) {
    while (*str) { ch(x, y, *str++, fg, s); x += 6 * s; }
  }
  // rechtsbuendig ab x
  void textR(int xRight, int y, const char* str, uint8_t fg, int s = 1) {
    text(xRight - textW(str, s), y, str, fg, s);
  }

  // Ein Zeichen um 90° im Uhrzeigersinn gedreht (liest oben->unten). Breite 7 px.
  void chV(int x, int y, char ci, uint8_t fg) {
    if (ci < 32 || ci > 126) ci = '?';
    const uint8_t* g = FONT5X7[ci - 32];
    for (int col = 0; col < 5; col++) {
      uint8_t bits = pgm_read_byte(&g[col]);
      for (int row = 0; row < 7; row++)
        if (bits & (1 << row)) pixel(x + (6 - row), y + col, fg);
    }
  }
  // Vertikaler Text (liest oben->unten). x = links, y = oben. Breite 7 px, Hoehe len*6.
  void textV(int x, int y, const char* str, uint8_t fg) {
    while (*str) { chV(x, y, *str++, fg); y += 6; }
  }

private:
  epaper_driver_display* _epd;
};

#endif
