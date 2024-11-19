#include "platform.h"
#include "driver/gpio.h"

void board_init() {
    // Enable system power domain

    gpio_reset_pin((gpio_num_t)POWER_EN_PIN);
    gpio_set_direction((gpio_num_t)POWER_EN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)POWER_EN_PIN, 1);
}

void platform_init() {

}
