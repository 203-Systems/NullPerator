#include "Adapters/node/hal/nullperator/midi/midi.h"
#include "board/pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "NP_MIDI";
static const uart_port_t MIDI_UART_NUM = static_cast<uart_port_t>(MIDI_UART);
static const int RX_BUF_SIZE = 1024;

namespace NullperatorHAL::MIDI {
    static bool driverInstalled = false;

    esp_err_t Init() {
        const uart_config_t uart_config = {
            .baud_rate = 31250,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122,
            .source_clk = UART_SCLK_DEFAULT,
        };

        esp_err_t ret = ESP_OK;
        if (!driverInstalled) {
            ret = uart_driver_install(MIDI_UART_NUM, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
                return ret;
            }
            driverInstalled = true;
        }

        ret = uart_param_config(MIDI_UART_NUM, &uart_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = uart_set_pin(
            MIDI_UART_NUM,
            MIDI_OUT_PIN,
            MIDI_IN_PIN,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
        );
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
            return ret;
        }

        ESP_LOGI(TAG, "MIDI initialized on UART%d (TX=%d, RX=%d)", MIDI_UART_NUM, MIDI_OUT_PIN, MIDI_IN_PIN);
        return ESP_OK;
    }

    uart_port_t GetPort() {
        return MIDI_UART_NUM;
    }

    esp_err_t Send(const uint8_t* data, size_t length) {
        int txBytes = uart_write_bytes(MIDI_UART_NUM, data, length);
        return (txBytes == static_cast<int>(length)) ? ESP_OK : ESP_FAIL;
    }

    int Receive(uint8_t* data, size_t maxLength, uint32_t timeoutMs) {
        return uart_read_bytes(MIDI_UART_NUM, data, maxLength, pdMS_TO_TICKS(timeoutMs));
    }
}
