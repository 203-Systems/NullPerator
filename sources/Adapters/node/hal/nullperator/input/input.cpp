#include "Adapters/node/hal/nullperator/input/input.h"
#include "board/pins.h"
#include "Adapters/node/hal/nullperator/system/system.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "NP_INPUT";

namespace NullperatorHAL::Input {
    namespace {
        constexpr uint16_t BUTTON_MASK =
            (1U << PCA_BTN_SELECT) |
            (1U << PCA_BTN_START) |
            (1U << PCA_BTN_RB) |
            (1U << PCA_BTN_DOWN) |
            (1U << PCA_BTN_B) |
            (1U << PCA_BTN_LEFT) |
            (1U << PCA_BTN_LB) |
            (1U << PCA_BTN_UP) |
            (1U << PCA_BTN_RIGHT) |
            (1U << PCA_BTN_A);

        constexpr uint16_t INPUT_MASK =
            BUTTON_MASK |
            (1U << PCA_BTN_CHRG) |
            (1U << PCA_BTN_FULL) |
            (1U << PCA_PHONE_DET);

        uint16_t s_inputCache = 0;
        constexpr uint32_t HEADPHONE_DEBOUNCE_MS = 500;
        bool s_headphoneCandidate = false;
        bool s_headphoneConnected = false;
        int64_t s_headphoneCandidateSinceUs = 0;

        void update_headphone_state(uint16_t input, bool initialize = false) {
            const bool detected = (input & (1U << PCA_PHONE_DET)) != 0;
            const int64_t nowUs = esp_timer_get_time();

            if (initialize) {
                s_headphoneCandidate = detected;
                s_headphoneConnected = detected;
                s_headphoneCandidateSinceUs = nowUs;
                return;
            }

            if (detected != s_headphoneCandidate) {
                s_headphoneCandidate = detected;
                s_headphoneCandidateSinceUs = nowUs;
                return;
            }

            constexpr int64_t kDebounceUs = HEADPHONE_DEBOUNCE_MS * 1000LL;
            if (s_headphoneConnected != detected &&
                nowUs - s_headphoneCandidateSinceUs >= kDebounceUs) {
                s_headphoneConnected = detected;
            }
        }

        esp_err_t configure_button_polarity() {
            if (!NullperatorHAL::System::SetIOExpanderPolarity(BUTTON_MASK)) {
                ESP_LOGE(TAG, "Failed to configure button polarity");
                return ESP_FAIL;
            }
            return ESP_OK;
        }

        uint16_t read_button_inputs_stable() {
            const uint16_t stable = NullperatorHAL::System::ReadIOExpander() & INPUT_MASK;
            s_inputCache = stable;
            return stable;
        }
    }

    esp_err_t Init() {
        if (!NullperatorHAL::System::GetI2CBus()) {
            ESP_LOGE(TAG, "System not initialized. Call System::Init() first.");
            return ESP_ERR_INVALID_STATE;
        }

        if (!NullperatorHAL::System::SetIOExpanderDirection(INPUT_MASK)) {
            ESP_LOGE(TAG, "Failed to configure input direction");
            return ESP_FAIL;
        }

        esp_err_t ret = configure_button_polarity();
        if (ret != ESP_OK) {
            return ret;
        }

        s_inputCache = read_button_inputs_stable();
        update_headphone_state(s_inputCache, true);

        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << FUNC_BTN_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);

        ESP_LOGI(TAG, "Input initialized");
        return ESP_OK;
    }

    ButtonState_t GetButtonState(bool* headphoneConnected) {
        ButtonState_t state = {};
        if (!NullperatorHAL::System::GetI2CBus()) {
            if (headphoneConnected) {
                *headphoneConnected = s_headphoneConnected;
            }
            return state;
        }

        const uint16_t level = read_button_inputs_stable();
        update_headphone_state(level);
        if (headphoneConnected) {
            *headphoneConnected = s_headphoneConnected;
        }

        // Button polarity is inverted in the PCA9555, so pressed reads back as high here.
        state.select = (level & (1U << PCA_BTN_SELECT)) != 0;
        state.start = (level & (1U << PCA_BTN_START)) != 0;
        state.rb = (level & (1U << PCA_BTN_RB)) != 0;
        state.lb = (level & (1U << PCA_BTN_LB)) != 0;
        // TODO(node): Restore the physical ENTER button once the stuck-button
        // hardware issue is resolved. USB Remote UI input remains available.
        state.a = false;
        state.b = (level & (1U << PCA_BTN_B)) != 0;
        state.up = (level & (1U << PCA_BTN_UP)) != 0;
        state.down = (level & (1U << PCA_BTN_DOWN)) != 0;
        state.left = (level & (1U << PCA_BTN_LEFT)) != 0;
        state.right = (level & (1U << PCA_BTN_RIGHT)) != 0;
        state.func = (gpio_get_level(static_cast<gpio_num_t>(FUNC_BTN_PIN)) == 0);

        return state;
    }
}
