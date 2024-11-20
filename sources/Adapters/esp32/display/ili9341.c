#include "ili9341.h"
#include "Adapters/esp32/platform/gpio.h"
#include "Adapters/esp32/platform/platform.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "soc/gpio_sig_map.h"
#include "freertos/FreeRTOS.h"
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
  // asm volatile("nop \n nop \n nop");
  // gpio_put(DISPLAY_CS, 0); // Active low
  // asm volatile("nop \n nop \n nop");
}

static inline void cs_deselect() {
  // asm volatile("nop \n nop \n nop");
  // gpio_put(DISPLAY_CS, 1);
  // asm volatile("nop \n nop \n nop");
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
    // If manually controlling CS, ensure CS is asserted
    ESP_ERROR_CHECK(spi_device_polling_transmit(display_handle, &trans));
    // If manually controlling CS, CS remains asserted until the end of all parameters
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
    // If manually controlling CS, ensure CS is asserted
    ESP_ERROR_CHECK(spi_device_polling_transmit(display_handle, &trans));
    // If manually controlling CS, CS remains asserted until data transfer is complete
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
  // ESP_LOGI("ILI9341", "Initializing display");
  gpio_reset_pin((gpio_num_t)DISPLAY_DC_PIN);
  gpio_set_direction((gpio_num_t)DISPLAY_DC_PIN, GPIO_MODE_OUTPUT);

  gpio_reset_pin((gpio_num_t)DISPLAY_BL_PIN);
  gpio_set_direction((gpio_num_t)DISPLAY_BL_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)DISPLAY_BL_PIN, 0);

  gpio_reset_pin((gpio_num_t)DISPLAY_RESET_PIN);
  gpio_set_direction((gpio_num_t)DISPLAY_RESET_PIN, GPIO_MODE_OUTPUT);

  // gpio_iomux_out(DISPLAY_SCK_PIN, SPICLK_OUT_IDX, false);

  // gpio_iomux_in(DISPLAY_MOSI_PIN, SPID_IN_IDX);
  // gpio_iomux_out(DISPLAY_MOSI_PIN, SPID_OUT_IDX, false);

  spi_bus_config_t buscfg = {
        .sclk_io_num = DISPLAY_SCK_PIN,
        .mosi_io_num = DISPLAY_MOSI_PIN,
        .miso_io_num = -1,
        .quadwp_io_num = -1, // Quad SPI LCD driver is not yet supported
        .quadhd_io_num = -1, // Quad SPI LCD driver is not yet supported
        .max_transfer_sz = 320 * 80 * sizeof(uint16_t), // transfer 80 lines of pixels (assume pixel is RGB565) at most in one SPI transaction
    };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO)); // Enable the DMA feature

  spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,     //Clock out at 10 MHz
        .mode = 0,                              //SPI mode 0
        .spics_io_num = -1,             //CS pin
        .queue_size = 7,                        //We want to be able to queue 7 transactions at a time
    };

  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &display_handle));

  sleep_ms(10);
  gpio_set_level((gpio_num_t)DISPLAY_RESET_PIN, 0);
  sleep_ms(10);
  gpio_set_level((gpio_num_t)DISPLAY_RESET_PIN, 1);

  ili9341_set_command(0x01); // soft reset
  sleep_ms(100);

  ili9341_set_command(ILI9341_GAMMASET);
  ili9341_command_param(0x01);

  // positive gamma correction
  ili9341_set_command(ILI9341_GMCTRP1);
  ili9341_write_data((uint8_t[15]){0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e,
                                   0xf1, 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09,
                                   0x00},
                     15);

  // negative gamma correction
  ili9341_set_command(ILI9341_GMCTRN1);
  ili9341_write_data((uint8_t[15]){0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31,
                                   0xc1, 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36,
                                   0x0f},
                     15);

  // memory access control
  ili9341_set_command(ILI9341_MADCTL);
  ili9341_command_param(0x88);

  // pixel format
  ili9341_set_command(ILI9341_PIXFMT);
  ili9341_command_param(0x55); // 16-bit

  // frame rate; default, 70 Hz
  ili9341_set_command(ILI9341_FRMCTR1);
  ili9341_command_param(0x00);
  ili9341_command_param(0x1B);

  // exit sleep
  ili9341_set_command(ILI9341_SLPOUT);

  // display on
  ili9341_set_command(ILI9341_DISPON);

  //

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

uint16_t swap_bytes(uint16_t color) { return (color >> 8) | (color << 8); }
