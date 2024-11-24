#pragma once
#include <cstdint>
#include "driver/i2c.h"
#include "esp_log.h"
#include <cstring>

namespace PCA95x5 {

namespace Reg {
    enum : uint8_t {
        INPUT_PORT_0,
        INPUT_PORT_1,
        OUTPUT_PORT_0,
        OUTPUT_PORT_1,
        POLARITY_INVERSION_PORT_0,
        POLARITY_INVERSION_PORT_1,
        CONFIGURATION_PORT_0,
        CONFIGURATION_PORT_1,
    };
}

namespace Port {
    enum Port : uint8_t {
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
}  // namespace Port

namespace Level {
    enum Level : uint8_t { L, H };
    enum LevelAll : uint16_t { L_ALL = 0x0000, H_ALL = 0xFFFF };
}  // namespace Level

namespace Polarity {
    enum Polarity : uint8_t { ORIGINAL, INVERTED };
    enum PolarityAll : uint16_t { ORIGINAL_ALL = 0x0000, INVERTED_ALL = 0xFFFF };
}  // namespace Polarity

namespace Direction {
    enum Direction : uint8_t { OUT, IN };
    enum DirectionAll : uint16_t { OUT_ALL = 0x0000, IN_ALL = 0xFFFF };
}  // namespace Direction

static constexpr uint8_t BASE_I2C_ADDR = 0x20;
class PCA95x5 {
    i2c_master_dev_handle_t device_handle;
    uint16_t input {0x0000};
    uint16_t output {0xFFFF};
    uint16_t pol {0x0000};
    uint16_t dir {0xFFFF};
    esp_err_t status {ESP_OK};
    uint16_t read_cache = 0;

public:
    void attach(i2c_master_dev_handle_t device_handle) {
        this->device_handle = device_handle;
    }

    uint16_t read() {
        read_bytes(Reg::INPUT_PORT_0, (uint8_t*)&this->input, 2);
        uint16_t read = this->input;
        read_bytes(Reg::INPUT_PORT_0, (uint8_t*)&this->input, 2);
        uint16_t read2 = this->input;

        if (read == read2) {
            
        } else {
            read_bytes(Reg::INPUT_PORT_0, (uint8_t*)&this->input, 2);
            uint16_t read3 = this->input;
            if (read == read3) {
                
            } else if (read2 == read3) {
                read = read2;
            } else {
                read = read_cache;
            }
        }
        read_cache = read;
        return read;
    }
    Level::Level read(const Port::Port port) {
        uint16_t v = read();
        return (v & (1 << port)) ? Level::H : Level::L;
    }

    bool write(const uint16_t value) {
        this->output = value;
        return write_impl();
    }
    bool write(const Port::Port port, const Level::Level level) {
        if (level == Level::H) {
            this->output |= (1 << port);
        } else {
            this->output &= ~(1 << port);
        }
        return write_impl();
    }

    bool polarity(const uint16_t value) {
        this->pol = value;
        return polarity_impl();
    }
    bool polarity(const Port::Port port, const Polarity::Polarity pol) {
        if (pol == Polarity::INVERTED) {
            this->pol |= (1 << port);
        } else {
            this->pol &= ~(1 << port);
        }
        return polarity_impl();
    }

    bool direction(const uint16_t value) {
        this->dir = value;
        return direction_impl();
    }

    bool direction(const Port::Port port, const Direction::Direction dir) {
        if (dir == Direction::IN) {
            this->dir |= (1 << port);
        } else {
            this->dir &= ~(1 << port);
        }
        return direction_impl();
    }

    esp_err_t i2c_error() const {
        return status;
    }

private:
    bool write_impl() {
        return write_bytes(Reg::OUTPUT_PORT_0, (const uint8_t*)&this->output, 2);
    }

    bool polarity_impl() {
        return write_bytes(Reg::POLARITY_INVERSION_PORT_0, (const uint8_t*)&this->pol, 2);
    }

    bool direction_impl() {
        return write_bytes(Reg::CONFIGURATION_PORT_0, (const uint8_t*)&this->dir, 2);
     }

    esp_err_t read_bytes(const uint8_t reg, uint8_t* data, const uint8_t size) {
    // Transmit register address and receive data in a single operation
    esp_err_t err = i2c_master_transmit_receive(
        this->device_handle,            // I2C device handle
        &reg,                           // Pointer to register address
        sizeof(reg),                    // Size of register address
        data,                           // Buffer to store received data
        size,                           // Number of bytes to read
        1000                            // Timeout in milliseconds
    );

    if (err != ESP_OK) {
        ESP_LOGE("PCA95x5", "I2C transmit-receive failed: %s", esp_err_to_name(err));
    }

    // ESP_LOGI("PCA95x5", "Inquiry: %02X Read: %02X %02X", reg, data[0], data[1]);
    return err;
}


    bool write_bytes(const uint8_t reg, const uint8_t* data, const uint8_t size) {
        // Create write buffer with register address and data
        uint8_t data_wr[size + 1];
        data_wr[0] = reg;
        memcpy(&data_wr[1], data, size);

        // ESP_LOGI("PCA95x5", "Write: %02X %02X %02X", reg, data[0], data[1]);

        esp_err_t err = i2c_master_transmit(this->device_handle, data_wr, sizeof(data_wr), 1000);
        status = err;
        return (err == ESP_OK);
    }
};

}  // namespace PCA95x5
