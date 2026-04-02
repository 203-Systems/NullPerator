#pragma once

#include <cstdint>
#include "esp_err.h"
#include "sdmmc_cmd.h"

namespace NullperatorHAL::Storage {
    esp_err_t Init();
    bool IsMounted();
    uint64_t GetCapacity();
    sdmmc_card_t* GetCard();
}
