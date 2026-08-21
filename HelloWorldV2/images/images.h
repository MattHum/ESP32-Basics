#ifndef IMAGES_H
#define IMAGES_H

#include <Arduino.h>
#include "doseJPG.h"
#include "KopfJPG.h"
#include "mueckeJPG.h"

#define IMAGE_COUNT 3

extern const unsigned char* const images[IMAGE_COUNT];
extern const char* const audio_files[IMAGE_COUNT];

#endif
