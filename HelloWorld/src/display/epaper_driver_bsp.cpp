#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "epaper_driver_bsp.h"
#include "esp_log.h"

#include "esp_heap_caps.h"

static const char *TAG = "driver";

epaper_driver_display::epaper_driver_display(int width, int height,custom_lcd_spi_t _lcd_spi_data) : 
    lcd_spi_data(_lcd_spi_data),
    Width(width),
    Height(height) {

    ESP_LOGI(TAG, "Initialize SPI");
	spi_port_init();
	spi_gpio_init();

    buffer = (uint8_t *)heap_caps_malloc(lcd_spi_data.buffer_len, MALLOC_CAP_DMA);
	assert(buffer);
}

epaper_driver_display::~epaper_driver_display() {

}

void epaper_driver_display::spi_gpio_init() {
    int rst = lcd_spi_data.rst;
    int cs = lcd_spi_data.cs;
    int dc = lcd_spi_data.dc;
    int busy = lcd_spi_data.busy;

    gpio_config_t gpio_conf = {};
	gpio_conf.intr_type = GPIO_INTR_DISABLE;
	gpio_conf.mode = GPIO_MODE_OUTPUT;
	gpio_conf.pin_bit_mask = (0x1ULL<<rst) | (0x1ULL<<dc) | (0x1ULL<<cs);
	gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
	ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

	gpio_conf.mode = GPIO_MODE_INPUT;
	gpio_conf.pin_bit_mask = (0x1ULL<<busy);
	gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    set_rst_1();
}

void epaper_driver_display::spi_port_init() {
    int mosi = lcd_spi_data.mosi;
    int scl = lcd_spi_data.scl;
    int spi_host = lcd_spi_data.spi_host;
    esp_err_t ret;
  	spi_bus_config_t buscfg = {};
  	  	buscfg.miso_io_num = -1;
  	  	buscfg.mosi_io_num = mosi;
  	  	buscfg.sclk_io_num = scl;
  	  	buscfg.quadwp_io_num = -1;
  	  	buscfg.quadhd_io_num = -1;
  	  	buscfg.max_transfer_sz = lcd_spi_data.buffer_len;

  	spi_device_interface_config_t devcfg = {};
  	  	devcfg.spics_io_num = -1;
  	  	devcfg.clock_speed_hz = 10 * 1000 * 1000;  //Clock out at 10 MHz
  	  	devcfg.mode = 0;                           //SPI mode 0
  	  	devcfg.queue_size = 7;                     //We want to be able to queue 7 transactions at a time

  	ret = spi_bus_initialize((spi_host_device_t)spi_host, &buscfg, SPI_DMA_CH_AUTO);
  	ESP_ERROR_CHECK(ret);
  	ret = spi_bus_add_device((spi_host_device_t)spi_host, &devcfg, &spi);
  	ESP_ERROR_CHECK(ret);
}

/* 1.54G busy is INVERTED: LOW = busy/working, HIGH = idle/complete */
void epaper_driver_display::read_busy() {
    int busy = lcd_spi_data.busy;
    vTaskDelay(pdMS_TO_TICKS(100));
    int timeout = 0;
    while(gpio_get_level((gpio_num_t)busy) == 0) 
	{
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout++;
        if (timeout > 3000) {           //30s timeout, don't hang forever
            Serial.printf("[EPD] read_busy TIMEOUT after 30s\n");
            return;
        }
    }
}

void epaper_driver_display::SPI_SendByte(uint8_t data) {
    esp_err_t ret;
  	spi_transaction_t t; 
  	memset(&t, 0, sizeof(t));
  	t.length = 8;      
  	t.tx_buffer = &data;
  	ret = spi_device_polling_transmit(spi, &t); //Transmit!
  	assert(ret == ESP_OK);                      //Should have had no issues.
}

void epaper_driver_display::EPD_SendData(uint8_t data) {
    set_dc_1();
  	set_cs_0();
  	SPI_SendByte(data);
  	set_cs_1();
}

void epaper_driver_display::EPD_SendCommand(uint8_t command) {
    set_dc_0();
  	set_cs_0();
  	SPI_SendByte(command);
  	set_cs_1();
}

void epaper_driver_display::writeBytes(uint8_t *buffer,int len) {
    set_dc_1();
  	set_cs_0();
  	esp_err_t ret;
  	spi_transaction_t t; 
  	memset(&t, 0, sizeof(t));
  	t.length = 8 * len;      
  	t.tx_buffer = buffer;
  	ret = spi_device_polling_transmit(spi, &t); //Transmit!
  	assert(ret == ESP_OK);
  	set_cs_1();
}

void epaper_driver_display::EPD_TurnOnDisplay() {
    EPD_SendCommand(0x12); // DISPLAY_REFRESH
    EPD_SendData(0x00);
    read_busy();
}

void epaper_driver_display::EPD_Init() {
    /* Software reset */
    set_rst_1();
    vTaskDelay(pdMS_TO_TICKS(200));
    set_rst_0();
    vTaskDelay(pdMS_TO_TICKS(2));
    set_rst_1();
    vTaskDelay(pdMS_TO_TICKS(200));

    Serial.printf("[EPD] busy level after reset = %d\n", gpio_get_level((gpio_num_t)lcd_spi_data.busy));

    EPD_SendCommand(0x4D);
    EPD_SendData(0x78);

    EPD_SendCommand(0x00); //PSR
    EPD_SendData(0x0F);
    EPD_SendData(0x29);

    EPD_SendCommand(0x06); //BTST_P
    EPD_SendData(0x0D);
    EPD_SendData(0x12);
    EPD_SendData(0x30);
    EPD_SendData(0x20);
    EPD_SendData(0x19);
    EPD_SendData(0x2A);
    EPD_SendData(0x22);

    EPD_SendCommand(0x50); //CDI
    EPD_SendData(0x37);

    EPD_SendCommand(0x61); //TRES
    EPD_SendData(Width/256);
    EPD_SendData(Width%256);
    EPD_SendData(Height/256);
    EPD_SendData(Height%256);

    EPD_SendCommand(0xE9);
    EPD_SendData(0x01);

    EPD_SendCommand(0x30); //PLL
    EPD_SendData(0x08);

    EPD_SendCommand(0x04); //POWER_ON
    read_busy();
}

void epaper_driver_display::EPD_Clear() {
    int buffer_len = lcd_spi_data.buffer_len;
    memset(buffer,0x55,buffer_len);  //4x white (0x01) per byte
}

void epaper_driver_display::EPD_Display() {
    int buffer_len = lcd_spi_data.buffer_len;
    EPD_SendCommand(0x10);
    assert(buffer);
    writeBytes(buffer,buffer_len);
    EPD_TurnOnDisplay();
}

void epaper_driver_display::EPD_DisplayPartBaseImage() {
    EPD_Display();
}

void epaper_driver_display::EPD_Init_Partial() {
    /* No partial refresh on 1.54G; nothing to do */
}

void epaper_driver_display::EPD_DisplayPart() {
    EPD_Display();
}

void epaper_driver_display::EPD_DrawColorPixel(uint16_t x, uint16_t y,uint8_t color) {
    if (x >= Width || y >= Height)
    {
        ESP_LOGE("EPD", "Out of bounds pixel: (%d,%d)", x, y);
        return; 
    }

    uint16_t byte_index = y * (Width/4) + (x >> 2);
    uint8_t shift = (3 - (x & 0x03)) * 2;
    buffer[byte_index] = (buffer[byte_index] & ~(0x03 << shift)) | ((color & 0x03) << shift);
}