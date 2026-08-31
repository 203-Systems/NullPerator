/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "UI2/Render/IUiPresenter.h"
#include "UI2/Render/UiDirtyTiles.h"
#include "UI2/Theme/UiPalette.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

struct IOSUiFrameRegion {
  ui2::DirtyStrip bounds{};
  std::vector<std::uint8_t> indices;
};

struct IOSUiFramePacket {
  std::uint32_t sequence = 0U;
  std::array<std::uint8_t, ui2::UiPalette::kColorCount * 3U> palette{};
  std::vector<IOSUiFrameRegion> regions;
};

class IOSUiPresenter final : public ui2::IUiPresenter {
public:
  static constexpr std::size_t Width = ui2::kScreenWidth;
  static constexpr std::size_t Height = ui2::kScreenHeight;

  IOSUiPresenter();
  ui2::PresentResult
  Present(const ui2::UiIndexedSurface &surface,
          const ui2::UiPalette &palette,
          std::span<const ui2::DirtyStrip> strips) override;
  bool DrainFrame(std::uint32_t afterSequence, IOSUiFramePacket &packet);

private:
  mutable std::mutex mutex_;
  std::array<std::uint8_t, Width * Height> indices_{};
  std::array<std::uint8_t, ui2::UiPalette::kColorCount * 3U> palette_{};
  ui2::UiDirtyTiles pendingDirty_{};
  std::uint32_t sequence_ = 1U;
  std::uint32_t lastDrainedSequence_ = 0U;
};
