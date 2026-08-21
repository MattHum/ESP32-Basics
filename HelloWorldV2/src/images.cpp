#include "images/images.h"

const unsigned char* const images[IMAGE_COUNT] = {
    doseJPG,
    KopfJPG,
    mueckeJPG,
};

const char* const audio_files[IMAGE_COUNT] = {
    nullptr,
    "/audio/DrEvil.mp3",
    "/audio/MacGyver.mp3",
};
