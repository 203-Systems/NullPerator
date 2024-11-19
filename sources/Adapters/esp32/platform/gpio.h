#ifndef _PLATFORM_PICO_GPIO_H_
#define _PLATFORM_PICO_GPIO_H_

// POWER
#define BATT_VOLTAGE_IN 1
#define BATT_VOLTAGE_ADC_CHANNEL ADC_CHANNEL_0
#define POWER_EN_PIN 9

// I2C
#define I2C_SDA 37
#define I2C_SCL 36

// Display
#define DISPLAY_SPI spi2 // Can't change this atm - linked to FSPICLK_IN_IDX, FSPID_IN_IDX, FSPID_OUT_IDX
#define DISPLAY_DC_PIN 33
#define DISPLAY_RESET_PIN 21
#define DISPLAY_SCK_PIN 47
#define DISPLAY_MOSI_PIN 48
#define DISPLAY_BL_PIN 12 

// MIDI
#define MIDI_BAUD_RATE 31250
#define MIDI_UART 
#define MIDI_OUT_PIN 35
#define MIDI_IN_PIN 42

/* SD Card */
#define SD_CLK_PIN 18
#define SD_CMD_PIN 14
#define SD_D0_PIN 16
#define SD_D1_PIN 13
#define SD_D2_PIN 17
#define SD_D3_PIN 15
#define SD_CD_PIN 11

// Sound
#define CODEC_MCLK     41
#define CODEC_DSDIN    40
#define CODEC_ASDOUT   39
#define CODEC_SCLK     38

// IO expander pins
#define INPUT_SELECT          0
#define INPUT_START           1
#define INPUT_CHRG            2
#define INPUT_STDBY           3
#define INPUT_RB              4
#define INPUT_DOWN            5
#define INPUT_B               6
#define INPUT_LEFT            7
#define INPUT_LB              8
#define OUTPUT_AUDIO_MUX_SEL  9
#define OUTPUT_PA_CTRL        10
#define INPUT_Y               11
#define INPUT_UP              12
#define INPUT_RIGHT           13
#define INPUT_X               14
#define INPUT_A               15

#endif
