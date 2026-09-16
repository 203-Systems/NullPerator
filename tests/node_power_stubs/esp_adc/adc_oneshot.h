#pragma once
#include "esp_err.h"

using adc_oneshot_unit_handle_t = void *;
inline constexpr int ADC_UNIT_1 = 0;
inline constexpr int ADC_ULP_MODE_DISABLE = 0;
inline constexpr int ADC_ATTEN_DB_12 = 12;
inline constexpr int ADC_BITWIDTH_12 = 12;
inline constexpr int ADC_CHANNEL_0 = 0;
struct adc_oneshot_unit_init_cfg_t {
  int unit_id;
  int ulp_mode;
};
struct adc_oneshot_chan_cfg_t {
  int atten;
  int bitwidth;
};
// ADC is deliberately unavailable: these tests exercise the charger GPIO path.
inline esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *,
                                     adc_oneshot_unit_handle_t *) {
  return ESP_FAIL;
}
inline esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, int,
                                           const adc_oneshot_chan_cfg_t *) {
  return ESP_FAIL;
}
inline esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, int, int *) {
  return ESP_FAIL;
}
