#include <Arduino.h>
#include "sdcard_bsp.h"
#include "SD_MMC.h"

bool sdcard_init() {
    Serial.println("[SD] Initializing SD card (SDIO 1-bit)...");

    if (!SD_MMC.setPins(39, 41, 40)) {
        Serial.println("[SD] setPins FAILED");
        return false;
    }

    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("[SD] SD mount FAILED");
        return false;
    }

    uint64_t totalBytes = SD_MMC.totalBytes();
    uint64_t usedBytes = SD_MMC.usedBytes();
    Serial.printf("[SD] Mounted: %llu MB total, %llu MB used\n", totalBytes / (1024*1024), usedBytes / (1024*1024));
    return true;
}

bool sdcard_fileExists(const char *path) {
    return SD_MMC.exists(path);
}
