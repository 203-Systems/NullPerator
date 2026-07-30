#pragma once

// POWER
#define BATT_VOLTAGE_IN_PIN 1
#define BATT_VOLTAGE_ADC_CHANNEL ADC_CHANNEL_0
#define BATT_VOLTAGE_DIV 0.5F // We have a /2 divider
#define BATT_VOLTAGE_CUTOFF 3.1F
#define BATT_VOLTAGE_FULL 4.35F

// I2C
#define I2C_SDA_PIN 47
#define I2C_SCL_PIN 48

// I2C Devices
#define ES8389_ADDR 0x10
#define PCA9555_ADDR 0x20
#define LSM6DS3TRC_ADDR 0x6A

// Display (240x240 ST7789V)
#define DISPLAY_DC_PIN 17
#define DISPLAY_RESET_PIN 9
#define DISPLAY_SCK_PIN 18
#define DISPLAY_MOSI_PIN 21
#define DISPLAY_BL_PIN 10

// MIDI
#define MIDI_UART 2
#define MIDI_OUT_PIN 46
#define MIDI_IN_PIN 42

// SDIO 4 lane high speed SD card
#define SD_CLK_PIN 16
#define SD_CMD_PIN 12
#define SD_D0_PIN 11
#define SD_D1_PIN 13
#define SD_D2_PIN 15
#define SD_D3_PIN 14

// Sound / I2S (ES8389)
#define I2S_MCLK_PIN 45
#define I2S_DOUT_PIN 40
#define I2S_DIN_PIN 38
#define I2S_BCLK_PIN 41
#define I2S_LRCK_PIN 39

// IO expander pins (PCA9555)
#define PCA_BTN_SELECT 0
#define PCA_BTN_START 1
#define PCA_BTN_CHRG 2
#define PCA_BTN_FULL 3
#define PCA_BTN_RB 4
#define PCA_BTN_DOWN 5
#define PCA_BTN_B 6
#define PCA_BTN_LEFT 7
#define PCA_BTN_LB 8
#define PCA_AUDIO_MUX_SEL 9
#define PCA_PA_CTRL 10
#define PCA_BTN_UP 12
#define PCA_BTN_RIGHT 13
#define PCA_PHONE_DET 14
#define PCA_BTN_A 15

// Non IO expander buttons
#define FUNC_BTN_PIN 8
