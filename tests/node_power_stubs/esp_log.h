#pragma once
#include <cstdint>
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
inline uint32_t esp_log_timestamp() { return 1; }
