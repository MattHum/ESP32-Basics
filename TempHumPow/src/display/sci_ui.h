#ifndef SCI_UI_H
#define SCI_UI_H

#include "epd_driver.h"

#define COLOR_BLACK 0x00
#define COLOR_WHITE 0x01

void sci_drawLine(EPDDriver &epd, int x0, int y0, int x1, int y1, uint8_t color);
void sci_drawRect(EPDDriver &epd, int x, int y, int w, int h, uint8_t color);
void sci_fillRect(EPDDriver &epd, int x, int y, int w, int h, uint8_t color);
void sci_drawCircle(EPDDriver &epd, int cx, int cy, int r, uint8_t color);
void sci_drawChar(EPDDriver &epd, int x, int y, char c, uint8_t color, int scale);
void sci_drawString(EPDDriver &epd, int x, int y, const char* str, uint8_t color, int scale);
void sci_drawDigitLarge(EPDDriver &epd, int x, int y, char digit, uint8_t color, int scale);
int  sci_stringWidth(const char* str, int scale);
void sci_renderHUD(EPDDriver &epd, float temp, float humi, float battV, int battPct);

#endif
