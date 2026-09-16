#pragma once

#include "esp_err.h"
#include <cstddef>
#include <cstdint>

using i2c_master_bus_handle_t = void *;
using i2c_master_dev_handle_t = void *;
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t *,
                                    std::size_t, uint8_t *, std::size_t, int);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t *,
                            std::size_t, int);
