#include <Arduino.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "epd_driver.h"
#include "esp_heap_caps.h"

EPDDriver::EPDDriver() : width(EPD_WIDTH), height(EPD_HEIGHT) {
    buffer = (uint8_t*)heap_caps_malloc(EPD_BUFFER_LEN, MALLOC_CAP_DMA);
    assert(buffer);
}

EPDDriver::~EPDDriver() {
    if (buffer) heap_caps_free(buffer);
}

void EPDDriver::init() {
    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num = -1;
    buscfg.mosi_io_num = EPD_PIN_MOSI;
    buscfg.sclk_io_num = EPD_PIN_SCK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = EPD_BUFFER_LEN;

    spi_device_interface_config_t devcfg = {};
    devcfg.spics_io_num = -1;
    devcfg.clock_speed_hz = 10 * 1000 * 1000;
    devcfg.mode = 0;
    devcfg.queue_size = 7;

    ESP_ERROR_CHECK(spi_bus_initialize((spi_host_device_t)EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device((spi_host_device_t)EPD_SPI_HOST, &devcfg, &spi));

    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = (1ULL << EPD_PIN_RST) | (1ULL << EPD_PIN_DC) | (1ULL << EPD_PIN_CS);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = (1ULL << EPD_PIN_BUSY);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    gpio_set_level((gpio_num_t)EPD_PIN_RST, 1);

    // Software reset
    gpio_set_level((gpio_num_t)EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level((gpio_num_t)EPD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level((gpio_num_t)EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    sendCmd(0x4D); sendData(0x78);
    sendCmd(0x00); sendData(0x0F); sendData(0x29);
    sendCmd(0x06); sendData(0x0D); sendData(0x12); sendData(0x30); sendData(0x20); sendData(0x19); sendData(0x2A); sendData(0x22);
    sendCmd(0x50); sendData(0x37);
    sendCmd(0x61); sendData(width / 256); sendData(width % 256); sendData(height / 256); sendData(height % 256);
    sendCmd(0xE9); sendData(0x01);
    sendCmd(0x30); sendData(0x08);
    sendCmd(0x04); readBusy();
}

void EPDDriver::readBusy() {
    vTaskDelay(pdMS_TO_TICKS(100));
    int timeout = 0;
    while (gpio_get_level((gpio_num_t)EPD_PIN_BUSY) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (++timeout > 3000) return;
    }
}

void EPDDriver::sendCmd(uint8_t cmd) {
    gpio_set_level((gpio_num_t)EPD_PIN_DC, 0);
    gpio_set_level((gpio_num_t)EPD_PIN_CS, 0);
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(spi, &t);
    gpio_set_level((gpio_num_t)EPD_PIN_CS, 1);
}

void EPDDriver::sendData(uint8_t data) {
    gpio_set_level((gpio_num_t)EPD_PIN_DC, 1);
    gpio_set_level((gpio_num_t)EPD_PIN_CS, 0);
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &data;
    spi_device_polling_transmit(spi, &t);
    gpio_set_level((gpio_num_t)EPD_PIN_CS, 1);
}

void EPDDriver::sendBytes(uint8_t* buf, int len) {
    gpio_set_level((gpio_num_t)EPD_PIN_DC, 1);
    gpio_set_level((gpio_num_t)EPD_PIN_CS, 0);
    spi_transaction_t t = {};
    t.length = 8 * len;
    t.tx_buffer = buf;
    spi_device_polling_transmit(spi, &t);
    gpio_set_level((gpio_num_t)EPD_PIN_CS, 1);
}

void EPDDriver::clear() {
    memset(buffer, 0x55, EPD_BUFFER_LEN);
}

void EPDDriver::display() {
    sendCmd(0x10);
    sendBytes(buffer, EPD_BUFFER_LEN);
    sendCmd(0x12);
    sendData(0x00);
    readBusy();
}

void EPDDriver::drawPixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    uint16_t byteIndex = y * (width / 4) + (x >> 2);
    uint8_t shift = (3 - (x & 0x03)) * 2;
    buffer[byteIndex] = (buffer[byteIndex] & ~(0x03 << shift)) | ((color & 0x03) << shift);
}
