# Nullperator HAL Library

Namespace-based HAL library for Nullperator ESP32-S3 hardware.

## Layout

- `nullperatorHAL.h/.cpp`: Board HAL that owns all module objects
- `board/pins.h`: Board pin map
- `system/`, `power/`, `input/`, `audio/`, `display/`, `imu/`, `storage/`, `midi/`: Module classes

## Usage

```cpp
#include "nullperatorHAL.h"

void app_main(void) {
    NullperatorHAL::Init();

    float voltage = NullperatorHAL::Power::GetBatteryVoltage();
    auto buttons = NullperatorHAL::Input::GetButtonState();
    auto imu = NullperatorHAL::IMU::GetData();
}
```

## Module Notes

- `System` owns the shared I2C bus and IO expander
- `Power`, `Input`, and `Audio` depend on `System`
- `Display`, `Storage`, and `MIDI` keep their state inside module-local static storage
- `IMU` is still a stub and returns zeroed data

## Common Methods

- `NullperatorHAL::Init()`
- `NullperatorHAL::Power::GetBatteryVoltage()`
- `NullperatorHAL::Power::GetBatteryPercentage()`
- `NullperatorHAL::Power::IsCharging()`
- `NullperatorHAL::Input::GetButtonState()`
- `NullperatorHAL::Audio::SetOutputMode(...)`
- `NullperatorHAL::Display::SetBrightness(...)`
- `NullperatorHAL::IMU::GetData()`
- `NullperatorHAL::Storage::IsMounted()`
- `NullperatorHAL::MIDI::Send(...)`

## Pin Configuration

See `board/pins.h` for the board pin map.
