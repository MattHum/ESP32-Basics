#ifndef EPD_DRIVER_H
#define EPD_DRIVER_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "user_config.h"

class EPDDriver {
private:
    spi_device_handle_t spi;
    uint8_t* buffer;
    int width, height;

    void readBusy();
    void sendCmd(uint8_t cmd);
    void sendData(uint8_t data);
    void sendBytes(uint8_t* buf, int len);

public:
    EPDDriver();
    ~EPDDriver();

    void init();
    void clear();
    void display();
    void drawPixel(int x, int y, uint8_t color);
    uint8_t* getBuffer() { return buffer; }
};

#endif
