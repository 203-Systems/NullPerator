#include "DisplayGammaCalibration.h"

#include "Adapters/node/display/Rgb565DisplayTransport.h"
#include "Adapters/node/hal/nullperator/display/display.h"
#include "Adapters/node/hal/nullperator/input/input.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

constexpr char kTag[] = "LCD_GAMMA_AB";
constexpr int kWidth = NullperatorHAL::Display::WIDTH;
constexpr int kHeight = NullperatorHAL::Display::HEIGHT;
constexpr int kChunkRows = 8;

struct GammaChoice {
  uint8_t commandValue;
  uint8_t major;
  uint8_t minor;
};

constexpr std::array<GammaChoice, 4> kGammaChoices{{
    {0x02, 1, 8},
    {0x01, 2, 2},
    {0x04, 2, 5},
    {0x08, 1, 0},
}};

DRAM_ATTR uint16_t gPixels[kWidth * kChunkRows];

constexpr uint16_t Rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>(((red & 0xf8U) << 8U) |
                               ((green & 0xfcU) << 3U) | (blue >> 3U));
}

constexpr uint16_t ToPanelOrder(uint16_t value) {
  return static_cast<uint16_t>((value >> 8U) | (value << 8U));
}

constexpr bool InRect(int x, int y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

constexpr std::array<uint8_t, 10> kDigitSegments{{
    0x3f, 0x06, 0x5b, 0x4f, 0x66,
    0x6d, 0x7d, 0x07, 0x7f, 0x6f,
}};

bool SegmentPixel(uint8_t digit, int localX, int localY) {
  if (digit > 9 || localX < 0 || localX >= 18 || localY < 0 ||
      localY >= 32) {
    return false;
  }
  const uint8_t mask = kDigitSegments[digit];
  const bool a = (mask & 0x01U) && InRect(localX, localY, 3, 0, 12, 3);
  const bool b = (mask & 0x02U) && InRect(localX, localY, 15, 3, 3, 11);
  const bool c = (mask & 0x04U) && InRect(localX, localY, 15, 18, 3, 11);
  const bool d = (mask & 0x08U) && InRect(localX, localY, 3, 29, 12, 3);
  const bool e = (mask & 0x10U) && InRect(localX, localY, 0, 18, 3, 11);
  const bool f = (mask & 0x20U) && InRect(localX, localY, 0, 3, 3, 11);
  const bool g = (mask & 0x40U) && InRect(localX, localY, 3, 14, 12, 3);
  return a || b || c || d || e || f || g;
}

uint16_t TestPixel(int x, int y, const GammaChoice &choice) {
  constexpr uint16_t black = Rgb565(0, 0, 0);
  constexpr uint16_t white = Rgb565(232, 239, 237);
  constexpr uint16_t cyan = Rgb565(70, 208, 220);
  constexpr uint16_t dim = Rgb565(63, 75, 73);

  // Large gamma number: "x.x".
  if (SegmentPixel(choice.major, x - 88, y - 10) ||
      SegmentPixel(choice.minor, x - 134, y - 10) ||
      InRect(x, y, 123, 36, 4, 4)) {
    return cyan;
  }

  // Full 0..255 neutral ramp. The black-end steps are deliberately wide so
  // crushing and elevated blacks are easy to spot on the physical panel.
  if (InRect(x, y, 8, 52, 224, 34)) {
    const int step = (x - 8) / 14;
    const uint8_t value = static_cast<uint8_t>(step * 17);
    return Rgb565(value, value, value);
  }

  if (InRect(x, y, 8, 94, 224, 24)) {
    constexpr std::array<uint16_t, 8> colors{{
        Rgb565(224, 42, 65), Rgb565(244, 174, 30),
        Rgb565(224, 218, 0), Rgb565(33, 211, 96),
        Rgb565(43, 182, 222), Rgb565(69, 102, 230),
        Rgb565(191, 76, 219), Rgb565(232, 239, 237),
    }};
    return colors[static_cast<std::size_t>((x - 8) / 28)];
  }

  // Representative UI2 semantic colors on the actual black background.
  if (InRect(x, y, 8, 126, 224, 30)) {
    constexpr std::array<uint16_t, 7> colors{{
        Rgb565(232, 239, 237), Rgb565(99, 111, 108),
        Rgb565(70, 208, 220), Rgb565(30, 210, 114),
        Rgb565(226, 193, 23), Rgb565(239, 61, 100),
        Rgb565(21, 34, 31),
    }};
    return colors[static_cast<std::size_t>((x - 8) / 32)];
  }

  // Fine dark ramp isolates the raised-black/low-contrast symptom.
  if (InRect(x, y, 8, 164, 224, 24)) {
    const int step = (x - 8) / 28;
    const uint8_t value = static_cast<uint8_t>(step * 5);
    return Rgb565(value, value, value);
  }

  // Minimal left/right/exit affordances without depending on the UI font.
  if ((x == 24 + (y - 216) && y >= 208 && y <= 216) ||
      (x == 24 - (y - 216) && y >= 216 && y <= 224) ||
      (x == 216 - (y - 216) && y >= 208 && y <= 216) ||
      (x == 216 + (y - 216) && y >= 216 && y <= 224)) {
    return dim;
  }
  if (InRect(x, y, 106, 204, 28, 28)) {
    const bool border = x == 106 || x == 133 || y == 204 || y == 231;
    return border ? white : black;
  }
  if (InRect(x, y, 114, 212, 12, 12)) {
    return cyan;
  }

  return black;
}

void DrawTestCard(const GammaChoice &choice) {
  for (int top = 0; top < kHeight; top += kChunkRows) {
    const int rows = (top + kChunkRows <= kHeight) ? kChunkRows
                                                   : (kHeight - top);
    for (int localY = 0; localY < rows; ++localY) {
      for (int x = 0; x < kWidth; ++x) {
        gPixels[localY * kWidth + x] =
            ToPanelOrder(TestPixel(x, top + localY, choice));
      }
    }
    (void)display_draw_rgb565_region(0, static_cast<uint16_t>(top), kWidth,
                                     static_cast<uint16_t>(rows), gPixels);
  }
}

bool AnyDirection(const NullperatorHAL::Input::ButtonState_t &buttons) {
  return buttons.left || buttons.right;
}

} // namespace

void RunDisplayGammaCalibrationIfRequested() {
  using NullperatorHAL::Input::GetButtonState;

  auto buttons = GetButtonState();
  if (!(buttons.select && buttons.start)) {
    return;
  }

  ESP_LOGW(kTag, "ST7789 gamma A/B mode entered");
  display_rgb565_transport_init();
  (void)NullperatorHAL::Display::SetBrightness(255);

  // GAMSET defaults to 2.2 after panel reset.
  std::size_t selected = 1;
  (void)NullperatorHAL::Display::SetGammaCurve(
      kGammaChoices[selected].commandValue);
  DrawTestCard(kGammaChoices[selected]);

  while (buttons.select || buttons.start) {
    vTaskDelay(pdMS_TO_TICKS(10));
    buttons = GetButtonState();
  }

  bool leftWasDown = false;
  bool rightWasDown = false;
  while (true) {
    buttons = GetButtonState();
    if (buttons.b) {
      while (GetButtonState().b) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      break;
    }

    bool changed = false;
    if (buttons.left && !leftWasDown) {
      selected = (selected + kGammaChoices.size() - 1) %
                 kGammaChoices.size();
      changed = true;
    } else if (buttons.right && !rightWasDown) {
      selected = (selected + 1) % kGammaChoices.size();
      changed = true;
    }
    leftWasDown = buttons.left;
    rightWasDown = buttons.right;

    if (changed) {
      const GammaChoice &choice = kGammaChoices[selected];
      ESP_LOGI(kTag, "GAMSET=0x%02x (gamma %u.%u)", choice.commandValue,
               choice.major, choice.minor);
      (void)NullperatorHAL::Display::SetGammaCurve(choice.commandValue);
      DrawTestCard(choice);
    }

    if (!AnyDirection(buttons)) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  ESP_LOGI(kTag, "Continuing with gamma %u.%u",
           kGammaChoices[selected].major, kGammaChoices[selected].minor);
  (void)NullperatorHAL::Display::SetBrightness(0);
}
