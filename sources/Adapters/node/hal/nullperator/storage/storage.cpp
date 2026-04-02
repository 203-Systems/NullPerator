#include "Adapters/node/hal/nullperator/storage/storage.h"
#include "board/pins.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include <cstdio>

static const char* TAG = "NP_STORAGE";

namespace NullperatorHAL::Storage {
    static sdmmc_card_t* card = nullptr;
    static bool mounted = false;

    esp_err_t Init() {
        if (mounted) {
            ESP_LOGW(TAG, "Storage already mounted");
            return ESP_OK;
        }

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024,
        };

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 4;
        slot_config.clk = static_cast<gpio_num_t>(SD_CLK_PIN);
        slot_config.cmd = static_cast<gpio_num_t>(SD_CMD_PIN);
        slot_config.d0 = static_cast<gpio_num_t>(SD_D0_PIN);
        slot_config.d1 = static_cast<gpio_num_t>(SD_D1_PIN);
        slot_config.d2 = static_cast<gpio_num_t>(SD_D2_PIN);
        slot_config.d3 = static_cast<gpio_num_t>(SD_D3_PIN);
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        esp_err_t ret =
            esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount filesystem");
            } else {
                ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
            }
            return ret;
        }

        mounted = true;

        sdmmc_card_print_info(stdout, card);
        ESP_LOGI(TAG, "SD card mounted at /sdcard");

        return ESP_OK;
    }

    bool IsMounted() {
        return mounted;
    }

    uint64_t GetCapacity() {
        if (!card) {
            return 0;
        }
        return static_cast<uint64_t>(card->csd.capacity) * card->csd.sector_size;
    }

    sdmmc_card_t* GetCard() {
        return card;
    }
}
