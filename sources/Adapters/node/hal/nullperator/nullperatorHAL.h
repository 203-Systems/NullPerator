#pragma once

#include "esp_err.h"
#include "Adapters/node/hal/nullperator/system/system.h"
#include "Adapters/node/hal/nullperator/power/power.h"
#include "Adapters/node/hal/nullperator/input/input.h"
#include "Adapters/node/hal/nullperator/audio/audio.h"
#include "Adapters/node/hal/nullperator/display/display.h"
#include "Adapters/node/hal/nullperator/imu/imu.h"
#include "Adapters/node/hal/nullperator/storage/storage.h"
#include "Adapters/node/hal/nullperator/midi/midi.h"

namespace NullperatorHAL {
    esp_err_t Init();
}
