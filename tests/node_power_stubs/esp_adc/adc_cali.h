#pragma once
#include "esp_err.h"

using adc_cali_handle_t = void *;
inline esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t, int, int *) {
  return ESP_FAIL;
}
