/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2SampleWaveformBackend.h"
#include "Application/UI2/Ui2SamplePathPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace ui2 {
namespace {

std::uint64_t IntegerSquareRoot(std::uint64_t value) {
  std::uint64_t result = 0U;
  std::uint64_t bit = std::uint64_t{1} << 62U;
  while (bit > value)
    bit >>= 2U;
  while (bit != 0U) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1U) + bit;
    } else {
      result >>= 1U;
    }
    bit >>= 2U;
  }
  return result;
}

} // namespace

bool Ui2SampleWaveformBackend::SetPath(const char *path) {
  path_.fill('\0');
  if (path == nullptr || path[0] == '\0')
    return false;
  const int written = std::snprintf(path_.data(), path_.size(), "%s", path);
  return written > 0 && static_cast<std::size_t>(written) < path_.size();
}

Ui2SampleWaveformLoadResult
Ui2SampleWaveformBackend::LoadPath(FileSystem &fileSystem, const char *path) {
  Reset();
  if (!SetPath(path))
    return Ui2SampleWaveformLoadResult::InvalidPath;

  auto file = fileSystem.Open(path_.data(), "r");
  if (!file) {
    Reset();
    return Ui2SampleWaveformLoadResult::OpenFailed;
  }
  auto header = WavHeaderWriter::ReadHeader(file.get());
  if (!header.has_value()) {
    Reset();
    return Ui2SampleWaveformLoadResult::InvalidWav;
  }
  if (header->numChannels == 0U || header->numChannels > 2U) {
    Reset();
    return Ui2SampleWaveformLoadResult::UnsupportedChannels;
  }
  const bool supportedPcm =
      header->audioFormat == 1U && header->bytesPerSample >= 1U &&
      header->bytesPerSample <= 4U;
  const bool supportedFloat =
      header->audioFormat == 3U &&
      (header->bytesPerSample == 4U || header->bytesPerSample == 8U);
  if (!supportedPcm && !supportedFloat) {
    Reset();
    return Ui2SampleWaveformLoadResult::UnsupportedEncoding;
  }
  if (header->blockAlign == 0U ||
      header->blockAlign > static_cast<std::uint16_t>(ReadBufferBytes)) {
    Reset();
    return Ui2SampleWaveformLoadResult::UnsupportedEncoding;
  }
  const std::uint32_t frames = header->dataChunkSize / header->blockAlign;
  if (frames == 0U) {
    Reset();
    return Ui2SampleWaveformLoadResult::Empty;
  }

  fileSystem_ = &fileSystem;
  header_ = *header;
  frameCount_ = frames;
  maxZoomLevel_ = 0U;
  std::uint32_t span = frameCount_;
  while (span > Ui2WaveformSnapshot::Width && maxZoomLevel_ < 16U) {
    span = (span + 1U) / 2U;
    ++maxZoomLevel_;
  }
  ready_ = true;
  UpdateWindow(0U);
  return Ui2SampleWaveformLoadResult::Loaded;
}

Ui2SampleWaveformLoadResult Ui2SampleWaveformBackend::LoadProjectPool(
    FileSystem &fileSystem, const char *projectName, const char *sampleName) {
  std::array<char, PFILENAME_SIZE> path{};
  if (projectName == nullptr || projectName[0] == '\0' ||
      !Ui2IsFlatProjectSampleLeaf(sampleName))
    return Ui2SampleWaveformLoadResult::InvalidPath;
  const int written = std::snprintf(path.data(), path.size(), "%s/%s/%s/%s",
                                    PROJECTS_DIR, projectName,
                                    PROJECT_SAMPLES_DIR, sampleName);
  if (written <= 0 || static_cast<std::size_t>(written) >= path.size())
    return Ui2SampleWaveformLoadResult::InvalidPath;
  return LoadPath(fileSystem, path.data());
}

Ui2SampleWaveformLoadResult
Ui2SampleWaveformBackend::LoadLibrary(FileSystem &fileSystem,
                                     const char *relativePath) {
  std::array<char, PFILENAME_SIZE> path{};
  if (relativePath == nullptr || relativePath[0] == '\0')
    return Ui2SampleWaveformLoadResult::InvalidPath;
  const int written = relativePath[0] == '/'
                          ? std::snprintf(path.data(), path.size(), "%s",
                                          relativePath)
                          : std::snprintf(path.data(), path.size(), "%s/%s",
                                          SAMPLES_LIB_DIR, relativePath);
  if (written <= 0 || static_cast<std::size_t>(written) >= path.size())
    return Ui2SampleWaveformLoadResult::InvalidPath;
  return LoadPath(fileSystem, path.data());
}

void Ui2SampleWaveformBackend::Reset() {
  fileSystem_ = nullptr;
  path_.fill('\0');
  header_ = {};
  amplitudes_.fill(0U);
  frameCount_ = 0U;
  viewStart_ = 0U;
  viewEnd_ = 0U;
  zoomLevel_ = 0U;
  maxZoomLevel_ = 0U;
  ready_ = false;
}

bool Ui2SampleWaveformBackend::UpdateWindow(std::uint32_t centerSample) {
  if (!ready_ || frameCount_ == 0U)
    return false;
  const std::uint32_t factor = std::uint32_t{1} << zoomLevel_;
  std::uint32_t span = frameCount_ / factor;
  span = std::clamp<std::uint32_t>(span, 1U, frameCount_);
  const std::uint32_t center = std::min(centerSample, frameCount_ - 1U);
  std::uint32_t start = center > span / 2U ? center - span / 2U : 0U;
  if (start + span > frameCount_)
    start = frameCount_ - span;
  const std::uint32_t end = start + span;
  const bool changed = start != viewStart_ || end != viewEnd_;
  viewStart_ = start;
  viewEnd_ = end;
  return changed;
}

bool Ui2SampleWaveformBackend::SetZoomLevel(std::uint8_t level,
                                            std::uint32_t centerSample) {
  if (!ready_)
    return false;
  const std::uint8_t next = std::min(level, maxZoomLevel_);
  const bool levelChanged = next != zoomLevel_;
  zoomLevel_ = next;
  return UpdateWindow(centerSample) || levelChanged;
}

bool Ui2SampleWaveformBackend::AdjustZoom(std::int8_t delta,
                                          std::uint32_t centerSample) {
  if (!ready_ || delta == 0)
    return false;
  const int next = std::clamp<int>(static_cast<int>(zoomLevel_) + delta, 0,
                                   static_cast<int>(maxZoomLevel_));
  return SetZoomLevel(static_cast<std::uint8_t>(next), centerSample);
}

bool Ui2SampleWaveformBackend::CenterOn(std::uint32_t sample) {
  return UpdateWindow(sample);
}

bool Ui2SampleWaveformBackend::PanColumns(std::int16_t columns) {
  if (!ready_ || columns == 0 || viewEnd_ <= viewStart_)
    return false;
  const std::uint32_t span = viewEnd_ - viewStart_;
  const std::int64_t step = std::max<std::uint32_t>(
      1U, span / static_cast<std::uint32_t>(Ui2WaveformSnapshot::Width));
  const std::int64_t center =
      static_cast<std::int64_t>(viewStart_) + span / 2U;
  const std::int64_t maximum = static_cast<std::int64_t>(frameCount_ - 1U);
  const std::int64_t next = std::clamp<std::int64_t>(
      center + static_cast<std::int64_t>(columns) * step, 0, maximum);
  return UpdateWindow(static_cast<std::uint32_t>(next));
}

std::int16_t Ui2SampleWaveformBackend::DecodeFirstChannel(
    const std::uint8_t *frame) const {
  if (header_.audioFormat == 1U) {
    switch (header_.bytesPerSample) {
    case 1U:
      return static_cast<std::int16_t>(
          (static_cast<std::int16_t>(frame[0]) - 128) * 256);
    case 2U: {
      const std::uint16_t raw = static_cast<std::uint16_t>(
          static_cast<std::uint32_t>(frame[0]) |
          (static_cast<std::uint32_t>(frame[1]) << 8U));
      return static_cast<std::int16_t>(raw);
    }
    case 3U: {
      std::uint32_t raw = static_cast<std::uint32_t>(frame[0]) |
                          (static_cast<std::uint32_t>(frame[1]) << 8U) |
                          (static_cast<std::uint32_t>(frame[2]) << 16U);
      if ((raw & 0x00800000U) != 0U)
        raw |= 0xFF000000U;
      return static_cast<std::int16_t>(static_cast<std::int32_t>(raw) >> 8U);
    }
    case 4U: {
      const std::uint32_t raw = static_cast<std::uint32_t>(frame[0]) |
                                (static_cast<std::uint32_t>(frame[1]) << 8U) |
                                (static_cast<std::uint32_t>(frame[2]) << 16U) |
                                (static_cast<std::uint32_t>(frame[3]) << 24U);
      return static_cast<std::int16_t>(static_cast<std::int32_t>(raw) >> 16U);
    }
    default:
      return 0;
    }
  }

  double value = 0.0;
  if (header_.bytesPerSample == 4U) {
    float sample = 0.0F;
    std::memcpy(&sample, frame, sizeof(sample));
    value = sample;
  } else if (header_.bytesPerSample == 8U) {
    std::memcpy(&value, frame, sizeof(value));
  }
  if (!std::isfinite(value))
    return 0;
  value = std::clamp(value, -1.0, 1.0);
  if (value <= -1.0)
    return std::numeric_limits<std::int16_t>::min();
  if (value >= 1.0)
    return std::numeric_limits<std::int16_t>::max();
  return static_cast<std::int16_t>(value * 32768.0);
}

Ui2SampleWaveformBuildResult Ui2SampleWaveformBackend::BuildMask(
    Ui2WaveformSnapshot &destination, std::uint8_t targetHeight) {
  destination = {};
  if (!ready_ || fileSystem_ == nullptr || viewEnd_ <= viewStart_ ||
      targetHeight == 0U)
    return Ui2SampleWaveformBuildResult::NotLoaded;

  auto file = fileSystem_->Open(path_.data(), "r");
  if (!file)
    return Ui2SampleWaveformBuildResult::OpenFailed;

  const std::uint32_t span = viewEnd_ - viewStart_;
  const std::uint32_t frameBytes = header_.blockAlign;
  file->Seek(static_cast<long>(header_.dataOffset + viewStart_ * frameBytes),
             SEEK_SET);
  sumSquares_.fill(0U);
  counts_.fill(0U);
  amplitudes_.fill(0U);

  const std::uint32_t framesPerRead =
      static_cast<std::uint32_t>(readBuffer_.size()) / frameBytes;
  if (framesPerRead == 0U)
    return Ui2SampleWaveformBuildResult::ReadFailed;
  std::uint32_t processed = 0U;
  while (processed < span) {
    const std::uint32_t frames =
        std::min<std::uint32_t>(framesPerRead, span - processed);
    const std::uint32_t bytes = frames * frameBytes;
    if (file->Read(readBuffer_.data(), static_cast<int>(bytes)) !=
        static_cast<int>(bytes)) {
      destination = {};
      return Ui2SampleWaveformBuildResult::ReadFailed;
    }
    for (std::uint32_t frame = 0U; frame < frames; ++frame) {
      const std::uint32_t relative = processed + frame;
      std::size_t column = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(relative) *
           Ui2WaveformSnapshot::Width) /
          span);
      column = std::min<std::size_t>(column,
                                     Ui2WaveformSnapshot::Width - 1U);
      const std::int16_t sample =
          DecodeFirstChannel(readBuffer_.data() + frame * frameBytes);
      const std::int32_t quantized = static_cast<std::int32_t>(sample) >> 8U;
      sumSquares_[column] += static_cast<std::uint64_t>(
          static_cast<std::int64_t>(quantized) * quantized);
      ++counts_[column];
    }
    processed += frames;
  }

  for (std::size_t column = 0U; column < amplitudes_.size(); ++column) {
    if (counts_[column] == 0U)
      continue;
    const std::uint64_t mean = sumSquares_[column] / counts_[column];
    const std::uint64_t rms = IntegerSquareRoot(mean);
    amplitudes_[column] = static_cast<std::uint8_t>(
        std::min<std::uint64_t>(targetHeight,
                                (rms * targetHeight + 127U) / 128U));
  }
  destination.Capture(amplitudes_.data(), amplitudes_.size(), targetHeight,
                      targetHeight);
  return Ui2SampleWaveformBuildResult::Built;
}

} // namespace ui2
