#include "platform.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "PCA95x5.h"

i2c_master_bus_handle_t i2c_handle = NULL;
i2c_master_dev_handle_t io_expander_handle = NULL;

PCA95x5::PCA95x5 io_expander;

void board_init() {
    // Enable system power domain

    gpio_reset_pin((gpio_num_t)POWER_EN_PIN);
    gpio_set_direction((gpio_num_t)POWER_EN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)POWER_EN_PIN, 1);

    gpio_reset_pin((gpio_num_t)INPUT_MENU_PIN);
    gpio_set_direction((gpio_num_t)INPUT_MENU_PIN, GPIO_MODE_INPUT);

    // Enable I2C
    gpio_reset_pin((gpio_num_t)I2C_SDA_PIN);
    gpio_reset_pin((gpio_num_t)I2C_SCL_PIN);
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)I2C_SDA_PIN,
        .scl_io_num = (gpio_num_t)I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = 1 // Internal pullups
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_handle));

    // Set up the io expander i2c device
    i2c_device_config_t io_expander_cfg = {
    .device_address = PCA95x5::BASE_I2C_ADDR,
    .scl_speed_hz = 400000,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_handle, &io_expander_cfg, &io_expander_handle));

    // Set up the io expander
    io_expander.attach(io_expander_handle);

    esp_err_t ret;

    // Set specific pins as input (binary: 1111100111111111)
    if (!io_expander.direction(0b1111100111111111)) {
        ESP_LOGE("KEYPAD", "Set dir returned error");
    }

    // Set polarity of specific pins
    if (!io_expander.polarity(0b1111100111111111)) {
        ESP_LOGE("KEYPAD", "Set polarity returned error");
    }

    switch_audio_mode(headphone_out);
    switch_speaker_mode(true);
}

void platform_init() {

}

uint16_t get_io_expander_input() {
    return io_expander.read();
}

void switch_audio_mode(audio_mode mode) {
    if(mode == headphone_out) {
        io_expander.write((PCA95x5::Port::Port)OUTPUT_AUDIO_MUX_SEL, PCA95x5::Level::L);
    } else if(mode == line_in) {
        io_expander.write((PCA95x5::Port::Port)OUTPUT_AUDIO_MUX_SEL, PCA95x5::Level::H);
    }
}

void switch_speaker_mode(bool on) {
    io_expander.write((PCA95x5::Port::Port)OUTPUT_PA_CTRL, on ? PCA95x5::Level::H : PCA95x5::Level::L);
}