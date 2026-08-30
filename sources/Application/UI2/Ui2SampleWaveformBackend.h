/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Instruments/WavHeader.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Views/Ui2SampleSnapshot.h"
#include "System/FileSystem/FileSystem.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ui2 {

// Kept equal to AudioFileStreamer's static looping buffer without coupling
// the fixed controller headers to the legacy Project/Instrument include graph.
inline constexpr std::uint32_t Ui2SingleCycleMaximumFrames = 600U;

enum class Ui2SampleWaveformLoadResult : std::uint8_t {
  Loaded,
  InvalidPath,
  OpenFailed,
  InvalidWav,
  UnsupportedChannels,
  UnsupportedEncoding,
  Empty,
};

enum class Ui2SampleWaveformBuildResult : std::uint8_t {
  Built,
  NotLoaded,
  OpenFailed,
  ReadFailed,
};

// File-backed, fixed-capacity waveform decimator shared by the native Sample
// Editor and Slices pages. Opening or changing a zoom window may touch the
// filesystem; ordinary frame capture only copies the already encoded packet.
// No sample-sized buffer, vector, string, or per-frame allocation is used.
class Ui2SampleWaveformBackend {
public:
  static constexpr std::uint8_t EditorMaskHeight = 72U;
  // The approved Slices waveform command occupies 78 px. Its focus/marker
  // viewport is 86 px tall; markers are emitted independently by the
  // controller and therefore are not baked into this coverage mask.
  static constexpr std::uint8_t SlicesMaskHeight = 78U;
  static constexpr std::uint8_t SlicesMarkerViewportHeight = 86U;

  [[nodiscard]] Ui2SampleWaveformLoadResult LoadPath(FileSystem &fileSystem,
                                                     const char *path);
  [[nodiscard]] Ui2SampleWaveformLoadResult
  LoadProjectPool(FileSystem &fileSystem, const char *projectName,
                  const char *sampleName);
  [[nodiscard]] Ui2SampleWaveformLoadResult
  LoadLibrary(FileSystem &fileSystem, const char *relativePath);

  void Reset();

  [[nodiscard]] bool Ready() const { return ready_; }
  [[nodiscard]] std::uint32_t FrameCount() const { return frameCount_; }
  [[nodiscard]] std::uint32_t SampleRate() const { return header_.sampleRate; }
  [[nodiscard]] std::uint16_t ChannelCount() const {
    return header_.numChannels;
  }
  [[nodiscard]] std::uint8_t ZoomLevel() const { return zoomLevel_; }
  [[nodiscard]] std::uint8_t MaxZoomLevel() const { return maxZoomLevel_; }
  [[nodiscard]] std::uint32_t ViewStart() const { return viewStart_; }
  [[nodiscard]] std::uint32_t ViewEnd() const { return viewEnd_; }
  [[nodiscard]] const char *Path() const { return path_.data(); }
  // This backend deliberately owns read/decimation only. This conservative
  // capability may become true only when rename, rewrite/rollback, and every
  // corresponding application command handler form one complete transaction.
  [[nodiscard]] bool SupportsEditorTransactions() const { return false; }

  bool SetZoomLevel(std::uint8_t level, std::uint32_t centerSample);
  bool AdjustZoom(std::int8_t delta, std::uint32_t centerSample);
  bool CenterOn(std::uint32_t sample);
  bool PanColumns(std::int16_t columns);

  [[nodiscard]] Ui2SampleWaveformBuildResult
  BuildMask(Ui2WaveformSnapshot &destination, std::uint8_t targetHeight);

private:
  static constexpr std::size_t ReadBufferBytes = 1024U;

  bool SetPath(const char *path);
  bool UpdateWindow(std::uint32_t centerSample);
  [[nodiscard]] std::int16_t DecodeFirstChannel(const std::uint8_t *frame) const;

  FileSystem *fileSystem_ = nullptr;
  std::array<char, PFILENAME_SIZE> path_{};
  WavHeaderInfo header_{};
  std::array<std::uint8_t, ReadBufferBytes> readBuffer_{};
  std::array<std::uint64_t, Ui2WaveformSnapshot::Width> sumSquares_{};
  std::array<std::uint32_t, Ui2WaveformSnapshot::Width> counts_{};
  std::array<std::uint8_t, Ui2WaveformSnapshot::Width> amplitudes_{};
  std::uint32_t frameCount_ = 0U;
  std::uint32_t viewStart_ = 0U;
  std::uint32_t viewEnd_ = 0U;
  std::uint8_t zoomLevel_ = 0U;
  std::uint8_t maxZoomLevel_ = 0U;
  bool ready_ = false;
};

static_assert(!std::is_polymorphic_v<Ui2SampleWaveformBackend>);
static_assert(sizeof(Ui2SampleWaveformBackend) <= 5'500U);

} // namespace ui2
