#ifndef SDCARD_BSP_H
#define SDCARD_BSP_H

#include <Arduino.h>

bool sdcard_init();
bool sdcard_fileExists(const char *path);

#endif
