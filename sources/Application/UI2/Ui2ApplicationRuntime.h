/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/UiEngine.h"
#include "UI2/Views/Song/UiSongView.h"

#include <array>
#include <cstdint>

class AppWindow;

namespace ui2 {

// Platform-neutral application-thread controller. Both WASM and firmware feed
// the same PicoTracker model into the same UI2 scene and raster pipeline; only
// IUiPresenter differs between targets.
class UiApplicationRuntime final {
public:
  explicit UiApplicationRuntime(IUiPresenter &presenter)
      : engine_(engineStorage_, presenter) {}

  [[nodiscard]] bool Supports(const AppWindow &window) const;
  [[nodiscard]] PresentResult Present(AppWindow &window);
  void Invalidate() { previousValid_ = false; }

  // Integer-only approximation of the legacy -60 dB..0 dB meter mapping.
  // VU fill is deliberately not part of the pixel-exact golden contract.
  static constexpr std::uint8_t VuTopFromAmplitude(std::uint16_t amplitude) {
    if (amplitude < 33U) return 153U;
    if (amplitude >= 32700U) return 0U;
    std::uint8_t exponent = 0;
    std::uint16_t value = amplitude;
    while (value > 1U) {
      value >>= 1U;
      ++exponent;
    }
    const std::uint32_t base = 1U << exponent;
    const std::uint32_t fraction =
        ((static_cast<std::uint32_t>(amplitude) - base) * 16U) / base;
    const std::uint32_t steps =
        (static_cast<std::uint32_t>(exponent - 5U) * 16U) + fraction;
    const std::uint32_t active = (steps * 153U) / 160U;
    return static_cast<std::uint8_t>(153U - active);
  }

private:
  struct SongFrameState {
    std::array<char, 17> name{};
    std::array<char, 6> elapsed{};
    std::array<std::array<std::uint8_t, 8>, 16> rows{};
    std::array<std::array<char, 5>, 8> notes{};
    std::array<std::int8_t, 8> playbackRows{-1, -1, -1, -1,
                                            -1, -1, -1, -1};
    std::array<std::uint8_t, 2> vuLevelTop{153, 153};
    std::uint8_t rowOffset = 0;
    std::uint8_t editRow = 0;
    std::uint8_t editTrack = 0;
    bool playing = false;
    UiPowerState power = UiPowerState::BatteryNormal;

    bool operator==(const SongFrameState &) const = default;
  };

  static UiSongViewData ViewDataFor(const SongFrameState &state);
  void CaptureSong(AppWindow &window, SongFrameState &state);

  UiEngineStorage engineStorage_{};
  UiEngine engine_;
  UiFrameScene scene_{};
  SongFrameState previousSong_{};
  SongFrameState currentSong_{};
  bool previousValid_ = false;
};

} // namespace ui2
