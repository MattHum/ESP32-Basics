#include "sci_ui.h"
#include "sci_font.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void sci_drawLine(EPDDriver &epd, int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        epd.drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void sci_drawRect(EPDDriver &epd, int x, int y, int w, int h, uint8_t color) {
    sci_drawLine(epd, x, y, x + w - 1, y, color);
    sci_drawLine(epd, x + w - 1, y, x + w - 1, y + h - 1, color);
    sci_drawLine(epd, x + w - 1, y + h - 1, x, y + h - 1, color);
    sci_drawLine(epd, x, y + h - 1, x, y, color);
}

void sci_fillRect(EPDDriver &epd, int x, int y, int w, int h, uint8_t color) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            epd.drawPixel(i, j, color);
}

void sci_drawCircle(EPDDriver &epd, int cx, int cy, int r, uint8_t color) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        epd.drawPixel(cx + x, cy + y, color);
        epd.drawPixel(cx + y, cy + x, color);
        epd.drawPixel(cx - y, cy + x, color);
        epd.drawPixel(cx - x, cy + y, color);
        epd.drawPixel(cx - x, cy - y, color);
        epd.drawPixel(cx - y, cy - x, color);
        epd.drawPixel(cx + y, cy - x, color);
        epd.drawPixel(cx + x, cy - y, color);
        y++;
        if (err < 0) { err += 2 * y + 1; }
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void sci_drawChar(EPDDriver &epd, int x, int y, char c, uint8_t color, int scale) {
    if (c < 32 || c > 126) return;
    const uint8_t *glyph = FONT_5X7[c - 32];
    for (int col = 0; col < FONT_W; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < FONT_H; row++) {
            if (line & (1 << row)) {
                sci_fillRect(epd, x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void sci_drawString(EPDDriver &epd, int x, int y, const char* str, uint8_t color, int scale) {
    int cx = x;
    while (*str) {
        sci_drawChar(epd, cx, y, *str, color, scale);
        cx += (FONT_W + 1) * scale;
        str++;
    }
}

int sci_stringWidth(const char* str, int scale) {
    int len = strlen(str);
    return len * (FONT_W + 1) * scale - scale;
}

void sci_drawDigitLarge(EPDDriver &epd, int x, int y, char digit, uint8_t color, int scale) {
    sci_drawChar(epd, x, y, digit, color, scale);
}

static void sci_drawCornerBrackets(EPDDriver &epd, uint8_t color) {
    int b = 199;
    int len = 12;
    int gap = 3;
    sci_drawLine(epd, gap, gap, gap + len, gap, color);
    sci_drawLine(epd, gap, gap, gap, gap + len, color);
    sci_drawLine(epd, b - gap, gap, b - gap - len, gap, color);
    sci_drawLine(epd, b - gap, gap, b - gap, gap + len, color);
    sci_drawLine(epd, gap, b - gap, gap + len, b - gap, color);
    sci_drawLine(epd, gap, b - gap, gap, b - gap - len, color);
    sci_drawLine(epd, b - gap, b - gap, b - gap - len, b - gap, color);
    sci_drawLine(epd, b - gap, b - gap, b - gap, b - gap - len, color);
}

static void sci_drawBatteryIcon(EPDDriver &epd, int x, int y, int pct, uint8_t color) {
    int bw = 50, bh = 14, tw = 4, th = 8;
    sci_drawRect(epd, x, y, bw, bh, color);
    sci_fillRect(epd, x + bw, y + (bh - th) / 2, tw, th, color);
    int inner_w = bw - 4;
    int fill = (pct * inner_w) / 100;
    if (fill > 0) {
        sci_fillRect(epd, x + 2, y + 2, fill, bh - 4, color);
    }
}

static void sci_drawBarGraph(EPDDriver &epd, int x, int y, int w, int h, float value, float maxVal, uint8_t color) {
    sci_drawRect(epd, x, y, w, h, color);
    int inner_w = w - 4;
    int fill = (int)((value / maxVal) * inner_w);
    if (fill > inner_w) fill = inner_w;
    if (fill < 0) fill = 0;
    if (fill > 0) {
        sci_fillRect(epd, x + 2, y + 2, fill, h - 4, color);
    }
}

void sci_renderHUD(EPDDriver &epd, float temp, float humi, float battV, int battPct) {
    epd.clear();

    sci_drawCornerBrackets(epd, COLOR_WHITE);

    sci_drawLine(epd, 20, 25, 179, 25, COLOR_WHITE);
    sci_drawString(epd, 50, 8, "ENV MONITOR", COLOR_WHITE, 2);

    sci_drawString(epd, 15, 32, "TEMP", COLOR_WHITE, 1);
    sci_drawLine(epd, 15, 42, 55, 42, COLOR_WHITE);

    char buf[16];
    int intPart = (int)temp;
    int decPart = (int)((temp - intPart) * 10);
    if (decPart < 0) decPart = -decPart;
    snprintf(buf, sizeof(buf), "%d.%d", intPart, decPart);
    sci_drawString(epd, 15, 46, buf, COLOR_WHITE, 3);

    int valW = sci_stringWidth(buf, 3);
    sci_drawString(epd, 15 + valW + 6, 54, "C", COLOR_WHITE, 2);

    sci_drawBarGraph(epd, 15, 80, 170, 10, temp + 10.0f, 50.0f, COLOR_WHITE);

    sci_drawLine(epd, 15, 96, 184, 96, COLOR_WHITE);

    sci_drawString(epd, 15, 102, "HUM", COLOR_WHITE, 1);
    sci_drawLine(epd, 15, 112, 55, 112, COLOR_WHITE);

    int humiInt = (int)humi;
    int humiDec = (int)((humi - humiInt) * 10);
    if (humiDec < 0) humiDec = -humiDec;
    snprintf(buf, sizeof(buf), "%d.%d", humiInt, humiDec);
    sci_drawString(epd, 15, 116, buf, COLOR_WHITE, 3);

    valW = sci_stringWidth(buf, 3);
    sci_drawString(epd, 15 + valW + 6, 124, "%", COLOR_WHITE, 2);

    sci_drawBarGraph(epd, 15, 144, 170, 10, humi, 100.0f, COLOR_WHITE);

    sci_drawLine(epd, 15, 160, 184, 160, COLOR_WHITE);

    sci_drawString(epd, 15, 166, "BAT", COLOR_WHITE, 1);

    sci_drawBatteryIcon(epd, 55, 165, battPct, COLOR_WHITE);

    snprintf(buf, sizeof(buf), "%d%%", battPct);
    sci_drawString(epd, 120, 166, buf, COLOR_WHITE, 2);

    snprintf(buf, sizeof(buf), "%.2fV", battV);
    sci_drawString(epd, 148, 168, buf, COLOR_WHITE, 1);

    for (int i = 0; i < 5; i++) {
        int dotX = 90 + i * 6;
        if (i < (battPct + 9) / 20) {
            sci_fillRect(epd, dotX, 190, 3, 3, COLOR_WHITE);
        } else {
            sci_drawRect(epd, dotX, 190, 3, 3, COLOR_WHITE);
        }
    }
}
