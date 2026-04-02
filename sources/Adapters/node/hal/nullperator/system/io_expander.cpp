#include "Adapters/node/hal/nullperator/system/io_expander.h"
#include "esp_log.h"
#include <cstring>

namespace {
enum Register : uint8_t {
    kInputPort0 = 0,
    kOutputPort0 = 2,
    kPolarityPort0 = 4,
    kConfigPort0 = 6,
};
}

namespace NullperatorHAL::System {
void IOExpander::Attach(i2c_master_dev_handle_t deviceHandle) {
    deviceHandle_ = deviceHandle;
}

uint16_t IOExpander::Read() {
    ReadBytes(kInputPort0, reinterpret_cast<uint8_t*>(&input_), 2);
    uint16_t read1 = input_;
    ReadBytes(kInputPort0, reinterpret_cast<uint8_t*>(&input_), 2);
    uint16_t read2 = input_;

    uint16_t stable = read1;
    if (read1 != read2) {
        ReadBytes(kInputPort0, reinterpret_cast<uint8_t*>(&input_), 2);
        uint16_t read3 = input_;
        if (read1 == read3) {
            stable = read1;
        } else if (read2 == read3) {
            stable = read2;
        } else {
            stable = readCache_;
        }
    }

    readCache_ = stable;
    return stable;
}

bool IOExpander::Write(uint16_t value) {
    output_ = value;
    return WriteOutput();
}

bool IOExpander::Write(Port port, Level level) {
    if (level == Level::High) {
        output_ |= (1u << static_cast<uint8_t>(port));
    } else {
        output_ &= ~(1u << static_cast<uint8_t>(port));
    }
    return WriteOutput();
}

bool IOExpander::SetPolarity(uint16_t value) {
    polarity_ = value;
    return WritePolarity();
}

bool IOExpander::SetDirection(uint16_t value) {
    direction_ = value;
    return WriteDirection();
}

bool IOExpander::SetDirection(Port port, Direction direction) {
    if (direction == Direction::In) {
        direction_ |= (1u << static_cast<uint8_t>(port));
    } else {
        direction_ &= ~(1u << static_cast<uint8_t>(port));
    }
    return WriteDirection();
}

esp_err_t IOExpander::GetI2cError() const { return status_; }

bool IOExpander::WriteOutput() {
    return WriteBytes(kOutputPort0, reinterpret_cast<const uint8_t*>(&output_), 2);
}

bool IOExpander::WritePolarity() {
    return WriteBytes(kPolarityPort0, reinterpret_cast<const uint8_t*>(&polarity_), 2);
}

bool IOExpander::WriteDirection() {
    return WriteBytes(kConfigPort0, reinterpret_cast<const uint8_t*>(&direction_), 2);
}

esp_err_t IOExpander::ReadBytes(uint8_t reg, uint8_t* data, uint8_t size) {
    esp_err_t err = i2c_master_transmit_receive(deviceHandle_, &reg, sizeof(reg),
                                                data, size, 1000);
    status_ = err;
    if (err != ESP_OK) {
        static uint32_t lastErrorMs = 0;
        const uint32_t nowMs = esp_log_timestamp();
        if ((lastErrorMs == 0) || (nowMs - lastErrorMs > 1000)) {
            lastErrorMs = nowMs;
            ESP_LOGE("IO_EXPANDER", "I2C read failed: %s", esp_err_to_name(err));
        }
    }
    return err;
}

bool IOExpander::WriteBytes(uint8_t reg, const uint8_t* data, uint8_t size) {
    uint8_t writeBuffer[3];
    writeBuffer[0] = reg;
    std::memcpy(&writeBuffer[1], data, size);
    esp_err_t err =
        i2c_master_transmit(deviceHandle_, writeBuffer, size + 1, 1000);
    status_ = err;
    return err == ESP_OK;
}
}  // namespace NullperatorHAL::System
