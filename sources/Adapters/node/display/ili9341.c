#include "ili9341.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Adapters/node/platform/gpio.h"
#include "Adapters/node/platform/platform.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "soc/gpio_sig_map.h"
#include "esp_log.h"

/*

 (pin 1) VCC        5V/3.3V power input
 (pin 2) GND        Ground
 (pin 3) CS         LCD chip select signal, low level enable
 (pin 4) RESET      LCD reset signal, low level reset
 (pin 5) DC/RS      LCD register / data selection signal; high level: register,
 low level: data (pin 6) SDI(MOSI)  SPI bus write data signal (pin 7) SCK SPI
 bus clock signal (pin 8) LED        Backlight control; if not controlled,
 connect 3.3V always bright (pin 9) SDO(MISO)  SPI bus read data signal;
 optional

 */

spi_device_handle_t display_handle;

static inline void sleep_ms(int ms) { 
  vTaskDelay(ms / portTICK_PERIOD_MS); 
}

static inline void cs_select() {
  // CS is managed by SPI driver
}

static inline void cs_deselect() {
  // CS is managed by SPI driver
}

void ili9341_set_command(uint8_t cmd) {
    // ESP_LOGI("ILI9341", "Set command 0x%02x", cmd);
    spi_transaction_t trans = {0};
    trans.length = 8;
    trans.flags = SPI_TRANS_USE_TXDATA;
    trans.tx_data[0] = cmd;

    gpio_set_level(DISPLAY_DC_PIN, 0); // DC low for command
    // If manually controlling CS:
    // gpio_set_level(DISPLAY_CS_PIN, 0); // Assert CS
    ESP_ERROR_CHECK(spi_device_polling_transmit(display_handle, &trans));
    // gpio_set_level(DISPLAY_CS_PIN, 1); // Deassert CS
    gpio_set_level(DISPLAY_DC_PIN, 1); // DC high after command
}

void ili9341_command_param(uint8_t data) {
    // ESP_LOGI("ILI9341", "Sending command parameter 0x%02x", data);
    spi_transaction_t trans = {0};
    trans.length = 8;
    trans.flags = SPI_TRANS_USE_TXDATA;
    trans.tx_data[0] = data;

    gpio_set_level(DISPLAY_DC_PIN, 1); // DC high for data
    // gpio_set_level(DISPLAY_CS_PIN, 0); // Assert CS
    ESP_ERROR_CHECK(spi_device_polling_transmit(display_handle, &trans));
    // gpio_set_level(DISPLAY_CS_PIN, 1); // Deassert CS
}

void ili9341_command_param16(uint16_t data) {
    ili9341_command_param(data >> 8);   // Send high byte
    ili9341_command_param(data & 0xFF); // Send low byte
}

void ili9341_write_data(void *buffer, int bytes) {
    spi_transaction_t trans = {0};
    trans.length = bytes * 8;       // Length in bits
    trans.tx_buffer = buffer;

    gpio_set_level(DISPLAY_DC_PIN, 1); // DC high for data
    // gpio_set_level(DISPLAY_CS_PIN, 0); // Assert CS
    ESP_ERROR_CHECK(spi_device_polling_transmit(display_handle, &trans));
    // gpio_set_level(DISPLAY_CS_PIN, 1); // Deassert CS
}

void ili9341_write_data_continuous(void *buffer, int bytes) {
    // This function can be the same as ili9341_write_data unless specific behavior is needed
    ili9341_write_data(buffer, bytes);
}

void ili9341_start_writing() {
    // ESP_LOGI("ILI9341", "Starting write");
    cs_select();
}

void ili9341_stop_writing() {
    // ESP_LOGI("ILI9341", "Stopping write");
    cs_deselect();
}


void ili9341_init() {
  ESP_LOGI("ILI9341", "Initializing display");
  gpio_reset_pin((gpio_num_t)DISPLAY_DC_PIN);
  gpio_set_direction((gpio_num_t)DISPLAY_DC_PIN, GPIO_MODE_OUTPUT);

  gpio_reset_pin((gpio_num_t)DISPLAY_BL_PIN);
  gpio_set_direction((gpio_num_t)DISPLAY_BL_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)DISPLAY_BL_PIN, 1);
  

  if (DISPLAY_RESET_PIN >= 0) {
    gpio_reset_pin((gpio_num_t)DISPLAY_RESET_PIN);
    gpio_set_direction((gpio_num_t)DISPLAY_RESET_PIN, GPIO_MODE_OUTPUT);
  }

  spi_bus_config_t buscfg = {
        .sclk_io_num = DISPLAY_SCK_PIN,
        .mosi_io_num = DISPLAY_MOSI_PIN,
        .miso_io_num = -1,
        .quadwp_io_num = -1, // Quad SPI LCD driver is not yet supported
        .quadhd_io_num = -1, // Quad SPI LCD driver is not yet supported
        .max_transfer_sz = 240 * 80 * sizeof(uint16_t), // transfer 80 lines of pixels (assume pixel is RGB565) at most in one SPI transaction
    };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO)); // Enable the DMA feature

  spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000,     //Clock out at 10 MHz
        .mode = 3,                              //SPI mode 0
        .spics_io_num = -1,             //CS pin
        .queue_size = 7,                        //We want to be able to queue 7 transactions at a time
    };

  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &display_handle));

  sleep_ms(10);
  if (DISPLAY_RESET_PIN >= 0) {
    gpio_set_level((gpio_num_t)DISPLAY_RESET_PIN, 0);
    sleep_ms(10);
    gpio_set_level((gpio_num_t)DISPLAY_RESET_PIN, 1);
  } else {
    // If no hardware reset line, rely on the soft reset below.
  }

  ili9341_set_command(0x01); // soft reset
  sleep_ms(100);

  ili9341_set_command(ILI9341_GAMMASET);
  ili9341_command_param(0x01);

#ifdef LCD_ST7789
  // gamma correction for ST7789V
  // src:
  // https://github.com/kiklhorn/esphome/blob/dbb824c937fda160c0f2165a29a8b1a9aee4fb43/esphome/components/st7789/st7789_init.h#L57
  // positive gamma correction
  ili9341_set_command(ILI9341_GMCTRP1);
  ili9341_write_data((uint8_t[14]){0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32,
                                   0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17},
                     14);
  // negative gamma correction
  ili9341_set_command(ILI9341_GMCTRN1);
  ili9341_write_data((uint8_t[14]){0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31,
                                   0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E},
                     14);

#else
  // positive gamma correction
  ili9341_set_command(ILI9341_GMCTRP1);
  ili9341_write_data((uint8_t[16]){0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e,
                                   0xf1, 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09,
                                   0x00},
                     15);

  // negative gamma correction
  ili9341_set_command(ILI9341_GMCTRN1);
  ili9341_write_data((uint8_t[15]){0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31,
                                   0xc1, 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36,
                                   0x0f},
                     15);
#endif

  // memory access control
  ili9341_set_command(ILI9341_MADCTL);

  // correct orientation and invert colors for ST7789
#ifdef LCD_ST7789
  // D7 D6 D5 D4 D3  D2 D1 D0
  // MY MX MV ML RGB MH -  -
  ili9341_command_param(0xC0);
  ili9341_set_command(ILI9341_INVON);

#else
  ili9341_command_param(0x88);
#endif

  // pixel format
  ili9341_set_command(ILI9341_PIXFMT);
  ili9341_command_param(0x55); // 16-bit

  // frame rate; default, 70 Hz
  ili9341_set_command(ILI9341_FRMCTR1);
  ili9341_command_param(0x00);
  ili9341_command_param(0x1B);

  // exit sleepz
  ili9341_set_command(ILI9341_SLPOUT);

  // display on
  ili9341_set_command(ILI9341_DISPON);

  
// #ifdef LCD_ST7789 
//   #if ILI9341_TFTWIDTH == 240 && ILI9341_TFTHEIGHT == 240
    ili9341_set_command(ILI9341_MADCTL);
    ili9341_command_param(0x80 | 0x20 | 0x00); //ST77XX_MADCTL_MY | ST77XX_MADCTL_MV | ST77XX_MADCTL_RGB
    // #endif
// #endif


  // column address set
  ili9341_set_command(ILI9341_CASET);
  ili9341_command_param(0x00);
  ili9341_command_param(0x00); // start column
  ili9341_command_param(0x00);
  ili9341_command_param(0xef); // end column -> 239

  // page address set
  ili9341_set_command(ILI9341_PASET);
  ili9341_command_param(0x00);
  ili9341_command_param(0x00); // start page
  ili9341_command_param(0x01);
  ili9341_command_param(0x3f); // end page -> 319

  ili9341_set_command(ILI9341_RAMWR);
}
