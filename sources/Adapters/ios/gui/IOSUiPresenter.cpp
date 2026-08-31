/* SPDX-License-Identifier: BSD-3-Clause */

#include "IOSUiPresenter.h"

#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Theme/UiPalette.h"

#include <algorithm>
#include <cstring>
#include <utility>

IOSUiPresenter::IOSUiPresenter() { pendingDirty_.MarkAll(); }

ui2::PresentResult IOSUiPresenter::Present(
    const ui2::UiIndexedSurface &surface, const ui2::UiPalette &palette,
    std::span<const ui2::DirtyStrip> strips) {
  std::lock_guard lock(mutex_);
  std::array<std::uint8_t, ui2::UiPalette::kColorCount * 3U> nextPalette{};
  for (std::size_t index = 0U; index < ui2::UiPalette::kColorCount; ++index) {
    const ui2::Rgb888 color = palette.Get(static_cast<ui2::PaletteIndex>(index));
    nextPalette[index * 3U] = color.red;
    nextPalette[index * 3U + 1U] = color.green;
    nextPalette[index * 3U + 2U] = color.blue;
  }
  const bool paletteChanged = nextPalette != palette_;
  if (paletteChanged) {
    palette_ = nextPalette;
    pendingDirty_.MarkAll();
  }

  bool pixelsChanged = false;
  for (const ui2::DirtyStrip strip : strips) {
    const std::uint16_t left = std::min<std::uint16_t>(strip.x, Width);
    const std::uint16_t top = std::min<std::uint16_t>(strip.y, Height);
    const std::uint16_t right = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(strip.x + strip.width, Width));
    const std::uint16_t bottom = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(strip.y + strip.height, Height));
    if (right <= left || bottom <= top) continue;
    const std::size_t rowBytes = right - left;
    for (std::uint16_t y = top; y < bottom; ++y) {
      const std::size_t offset = static_cast<std::size_t>(y) * Width + left;
      std::memcpy(indices_.data() + offset, surface.Pixels().data() + offset,
                  rowBytes);
    }
    pendingDirty_.Mark(
        {static_cast<std::int16_t>(left), static_cast<std::int16_t>(top),
         static_cast<std::int16_t>(right - left),
         static_cast<std::int16_t>(bottom - top)});
    pixelsChanged = true;
  }
  if (paletteChanged || pixelsChanged) ++sequence_;
  return ui2::PresentResult::Presented;
}

bool IOSUiPresenter::DrainFrame(std::uint32_t afterSequence,
                                IOSUiFramePacket &packet) {
  std::lock_guard lock(mutex_);
  packet = {};
  packet.sequence = sequence_;
  if (afterSequence == sequence_) return false;

  ui2::DirtyStripList strips;
  const bool needsFullFrame =
      afterSequence != lastDrainedSequence_ || !pendingDirty_.Any() ||
      !pendingDirty_.Collect(strips);
  if (needsFullFrame) {
    strips.Clear();
    (void)strips.Push({0U, 0U, static_cast<std::uint16_t>(Width),
                      static_cast<std::uint16_t>(Height)});
  }

  packet.palette = palette_;
  packet.regions.reserve(strips.Size());
  for (const ui2::DirtyStrip bounds : strips.Strips()) {
    IOSUiFrameRegion region;
    region.bounds = bounds;
    region.indices.resize(static_cast<std::size_t>(bounds.width) * bounds.height);
    for (std::uint16_t row = 0U; row < bounds.height; ++row) {
      const std::size_t sourceOffset =
          static_cast<std::size_t>(bounds.y + row) * Width + bounds.x;
      const std::size_t destinationOffset =
          static_cast<std::size_t>(row) * bounds.width;
      std::memcpy(region.indices.data() + destinationOffset,
                  indices_.data() + sourceOffset, bounds.width);
    }
    packet.regions.push_back(std::move(region));
  }
  pendingDirty_.Clear();
  lastDrainedSequence_ = sequence_;
  return true;
}
