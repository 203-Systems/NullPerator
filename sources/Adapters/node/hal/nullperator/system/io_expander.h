#pragma once

#include <cstdint>
#include "driver/i2c_master.h"
#include "esp_err.h"

namespace NullperatorHAL::System {
class IOExpander {
public:
    enum class Port : uint8_t {
        P00,
        P01,
        P02,
        P03,
        P04,
        P05,
        P06,
        P07,
        P10,
        P11,
        P12,
        P13,
        P14,
        P15,
        P16,
        P17,
    };

    enum class Level : uint8_t { Low, High };
    enum class Direction : uint8_t { Out, In };

    static constexpr uint8_t kBaseAddress = 0x20;
    static constexpr uint16_t kAllHigh = 0xFFFF;

    void Attach(i2c_master_dev_handle_t deviceHandle);
    uint16_t Read();
    bool Write(uint16_t value);
    bool Write(Port port, Level level);
    bool SetPolarity(uint16_t value);
    bool SetDirection(uint16_t value);
    bool SetDirection(Port port, Direction direction);
    esp_err_t GetI2cError() const;

private:
    bool WriteOutput();
    bool WritePolarity();
    bool WriteDirection();
    esp_err_t ReadBytes(uint8_t reg, uint8_t* data, uint8_t size);
    bool WriteBytes(uint8_t reg, const uint8_t* data, uint8_t size);

    i2c_master_dev_handle_t deviceHandle_ = nullptr;
    uint16_t input_ = 0x0000;
    uint16_t output_ = 0xFFFF;
    uint16_t polarity_ = 0x0000;
    uint16_t direction_ = 0xFFFF;
    esp_err_t status_ = ESP_OK;
    uint16_t readCache_ = 0;
};
}  // namespace NullperatorHAL::System
