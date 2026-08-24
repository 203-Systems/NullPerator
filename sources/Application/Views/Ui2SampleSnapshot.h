/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// The legacy GraphField keeps 320 one-byte RMS amplitudes. UI2 renders a
// 222-column waveform from a copied packet, so the application thread can
// release the legacy view before the frame is rasterized. The packet budget
// leaves room in UiContentScene's 1 KiB payload for labels and values.
struct Ui2WaveformSnapshot {
  static constexpr std::size_t Width = 222;
  static constexpr std::size_t MaxEncodedBytes = 896;

  std::array<std::uint8_t, MaxEncodedBytes> encoded{};
  std::uint16_t size = 0;
  std::uint32_t revision = 0;

  [[nodiscard]] std::span<const std::uint8_t> Mask() const {
    return {encoded.data(), size};
  }

  // Produces UiCommandList::SparseCoverageMask's column format. When a loud
  // waveform would exceed the fixed budget, all columns are reduced by the
  // same integer divisor. This keeps the envelope continuous and bounded
  // without allocating or silently dropping the right side of the sample.
  void Capture(const std::uint8_t *source, std::size_t sourceWidth,
               std::uint8_t sourceHeight, std::uint8_t targetHeight) {
    encoded.fill(0);
    size = 0;

    if (source == nullptr || sourceWidth == 0U || sourceHeight == 0U ||
        targetHeight == 0U) {
      revision = 0;
      return;
    }

    std::array<std::uint8_t, Width> amplitudes{};
    for (std::size_t x = 0; x < Width; ++x) {
      const std::size_t begin = (x * sourceWidth) / Width;
      std::size_t end = ((x + 1U) * sourceWidth + Width - 1U) / Width;
      end = std::clamp(end, begin + 1U, sourceWidth);
      std::uint8_t peak = 0;
      for (std::size_t sample = begin; sample < end; ++sample)
        peak = std::max(peak, source[sample]);
      const std::uint16_t scaled = static_cast<std::uint16_t>(peak) *
                                   static_cast<std::uint16_t>(targetHeight);
      amplitudes[x] = static_cast<std::uint8_t>(std::min<std::uint16_t>(
          targetHeight, static_cast<std::uint16_t>(
                            (scaled + sourceHeight - 1U) / sourceHeight)));
    }

    const auto reducedRun = [](std::uint8_t amplitude,
                               std::uint8_t divisor) -> std::uint8_t {
      if (amplitude == 0U)
        return 0U;
      return static_cast<std::uint8_t>((amplitude + divisor - 1U) / divisor);
    };
    const auto encodedSize = [&](std::uint8_t divisor) {
      std::size_t bytes = Width * 2U;
      for (const std::uint8_t amplitude : amplitudes)
        bytes += (reducedRun(amplitude, divisor) + 3U) / 4U;
      return bytes;
    };

    std::uint8_t divisor = 1U;
    while (divisor < targetHeight &&
           encodedSize(divisor) > MaxEncodedBytes)
      ++divisor;

    std::size_t cursor = 0;
    for (const std::uint8_t amplitude : amplitudes) {
      const std::uint8_t run = reducedRun(amplitude, divisor);
      if (run == 0U) {
        encoded[cursor++] = 0xFFU;
        encoded[cursor++] = 0U;
        continue;
      }

      const std::uint8_t start =
          static_cast<std::uint8_t>((targetHeight - run) / 2U);
      encoded[cursor++] = start;
      encoded[cursor++] = run;
      const std::size_t packedStart = cursor;
      const std::size_t packedBytes = (run + 3U) / 4U;
      cursor += packedBytes;

      for (std::uint8_t row = 0; row < run; ++row) {
        // 2-bit values are stored as coverage minus one. A half-covered edge
        // gives the waveform a stable antialiased contour at 1x rendering.
        std::uint8_t coverage = 3U;
        if (run > 1U && (row == 0U || row + 1U == run))
          coverage = 1U;
        encoded[packedStart + row / 4U] |=
            static_cast<std::uint8_t>(coverage << ((row % 4U) * 2U));
      }
    }
    size = static_cast<std::uint16_t>(cursor);

    std::uint32_t hash = 2166136261U;
    for (std::size_t index = 0; index < size; ++index) {
      hash ^= encoded[index];
      hash *= 16777619U;
    }
    revision = hash;
  }
};

enum class Ui2WaveformMarkerKind : std::uint8_t {
  Start,
  End,
  Slice,
  Playhead,
};

struct Ui2WaveformMarkerSnapshot {
  std::uint8_t x = 0;
  Ui2WaveformMarkerKind kind = Ui2WaveformMarkerKind::Slice;
  bool selected = false;

  bool operator==(const Ui2WaveformMarkerSnapshot &) const = default;
};

template <std::size_t Capacity> struct Ui2WaveformMarkersSnapshot {
  std::array<Ui2WaveformMarkerSnapshot, Capacity> markers{};
  std::uint8_t count = 0;

  void Push(std::uint8_t x, Ui2WaveformMarkerKind kind, bool selected) {
    if (count >= Capacity)
      return;
    markers[count++] = {x, kind, selected};
  }
};

inline std::uint8_t Ui2WaveformX(std::uint32_t sample,
                                std::uint32_t viewStart,
                                std::uint32_t viewEnd) {
  if (viewEnd <= viewStart || sample <= viewStart)
    return 0U;
  if (sample >= viewEnd)
    return static_cast<std::uint8_t>(Ui2WaveformSnapshot::Width - 1U);
  const std::uint64_t relative = sample - viewStart;
  const std::uint64_t span = viewEnd - viewStart;
  return static_cast<std::uint8_t>(
      (relative * (Ui2WaveformSnapshot::Width - 1U)) / span);
}

template <std::size_t Size>
void CopyUi2SnapshotText(std::array<char, Size> &destination,
                         std::string_view source) {
  static_assert(Size > 0U);
  destination.fill('\0');
  const std::size_t count = std::min(source.size(), Size - 1U);
  std::copy_n(source.begin(), count, destination.begin());
}

static_assert(Ui2WaveformSnapshot::MaxEncodedBytes <= 896U);
