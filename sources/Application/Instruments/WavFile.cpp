#include "Application/Instruments/WavReadPolicy.h"
/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Application/Model/Config.h"
#include "Application/Model/Song.h"
#include "Foundation/Types/Types.h"
#include "Services/Audio/WavHeader.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/I_File.h"
#include "WavFile.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdlib.h>
#include <type_traits>

unsigned char WavFile::readBuffer_[BUFFER_SIZE];
int16_t WavFile::convertedBuffer_[BUFFER_SIZE / 2];

int16_t ClampToInt16(double sample) {
  if (std::isnan(sample))
    return 0;
  if (sample <= -1.0)
    return -32768;
  if (sample >= +1.0)
    return +32767;
  return static_cast<int16_t>(sample * 32768.0);
}

int16_t ConvertSampleToInt16(const uint8_t *samplePtr, uint16_t audioFormat,
                             int32_t bytePerSample) {

  if (audioFormat == 1) { // PCM
    switch (bytePerSample) {
    case 1: {
      // expand 8-bit to 16-bit
      // 8-bit PCM is unsigned while >8-bit is signed
      return static_cast<int16_t>((static_cast<int16_t>(samplePtr[0]) - 128)
                                  << 8);
    }
    case 2: {
      // signed 16-bit
      int16_t value;
      memcpy(&value, samplePtr, sizeof(value));
      return value;
    }
    case 3: {
      // signed 24-bit
      int32_t value = samplePtr[0] | (samplePtr[1] << 8) |
                      (static_cast<int32_t>(samplePtr[2]) << 16);
      value = (value << 8) >> 8; // Sign extend
      return static_cast<int16_t>(value >> 8);
    }
    case 4: {
      // signed 32-bit
      int32_t value;
      memcpy(&value, samplePtr, sizeof(value));
      return static_cast<int16_t>(value >> 16);
    }
    default:
      break;
    }
  } else if (audioFormat == 3) { // IEEE float
    if (bytePerSample == 4) {
      float value;
      memcpy(&value, samplePtr, sizeof(value));
      return ClampToInt16(static_cast<double>(value));
    }
    if (bytePerSample == 8) {
      double value;
      memcpy(&value, samplePtr, sizeof(value));
      return ClampToInt16(value);
    }
  }

  Trace::Error("WAVFILE: Unsupported format (%u) or byte depth (%d)",
               audioFormat, bytePerSample);
  return 0;
}

float ConvertSampleToFloat(const uint8_t *samplePtr, uint16_t audioFormat,
                           int32_t bytePerSample) {
  if (audioFormat == 1) { // PCM
    switch (bytePerSample) {
    case 1: {
      int16_t v = static_cast<int16_t>(samplePtr[0]) - 128;
      return static_cast<float>(v) / 128.0f;
    }
    case 2: {
      int16_t value;
      memcpy(&value, samplePtr, sizeof(value));
      return static_cast<float>(value) / 32768.0f;
    }
    case 3: {
      int32_t value = samplePtr[0] | (samplePtr[1] << 8) |
                      (static_cast<int32_t>(samplePtr[2]) << 16);
      value = (value << 8) >> 8; // Sign extend
      return static_cast<float>(value) / 8388608.0f;
    }
    case 4: {
      int32_t value;
      memcpy(&value, samplePtr, sizeof(value));
      return static_cast<float>(value) / 2147483648.0f;
    }
    default:
      break;
    }
  } else if (audioFormat == 3) { // IEEE float
    if (bytePerSample == 4) {
      float value;
      memcpy(&value, samplePtr, sizeof(value));
      return std::isfinite(value) ? value : 0.0F;
    }
    if (bytePerSample == 8) {
      double value;
      memcpy(&value, samplePtr, sizeof(value));
      return std::isfinite(value) &&
                     std::abs(value) <= std::numeric_limits<float>::max()
                 ? static_cast<float>(value)
                 : 0.0F;
    }
  }

  Trace::Error("WAVFILE: Unsupported format (%u) or byte depth (%d)",
               audioFormat, bytePerSample);
  return 0.0f;
}

WavFile::WavFile()
    : file_(), samples_(nullptr), sampleBufferSize_(0), size_(0),
      sampleRate_(0), channelCount_(0), bytePerSample_(0), audioFormat_(0),
      dataPosition_(0), readCount_(0) {}

etl::expected<void, WAVEFILE_ERROR> WavFile::Open(const char *name) {
  // open file
  FileSystem *fs = FileSystem::GetInstance();
  auto file = fs->Open(name, "r");

  if (!file)
    return etl::unexpected(INVALID_FILE);

  auto header = ReadTrackerWavHeader(file.get());
  if (!header) {
    return etl::unexpected(header.error());
  }

  file_ = std::move(file);

  sampleRate_ = header->sampleRate;
  channelCount_ = header->numChannels;
  bytePerSample_ = header->bytesPerSample;
  audioFormat_ = header->audioFormat;

  Trace::Debug("File data bytes: %u", header->dataChunkSize);

  size_ =
      header->dataChunkSize / (header->numChannels * header->bytesPerSample);
  Trace::Debug("File sample count: %i", size_);

  // All samples are saved as 16bit/sample in memory
  sampleBufferSize_ = size_ * header->numChannels * 2;
  Trace::Debug("File sampleBufferSize_: %i", sampleBufferSize_);

  readCount_ = header->dataChunkSize;
  dataPosition_ = header->dataOffset;

  samples_ = nullptr;

  file_->Seek(header->dataOffset, SEEK_SET);
  return {};
};

void *WavFile::GetSampleBuffer(int note) { return samples_; };

void WavFile::SetSampleBuffer(short *ptr) { samples_ = ptr; }

int WavFile::GetSize(int note) { return size_; };

int WavFile::GetChannelCount(int note) { return channelCount_; };

int WavFile::GetSampleRate(int note) { return sampleRate_; };

float WavFile::GetLengthInSec() { return (float)size_ / sampleRate_; };

long WavFile::readBlock(long start, long size) {
  if (!file_ || size <= 0 || size > BUFFER_SIZE) {
    return 0;
  }
  file_->Seek(start, SEEK_SET);
  return file_->Read(readBuffer_, static_cast<int>(size));
};

bool WavFile::GetBuffer(long start, long size) {
  if (!file_ || start < 0 || size <= 0 || start >= size_) {
    return false;
  }

  const int64_t totalSamplesWide = static_cast<int64_t>(size) * channelCount_;
  const int32_t maxSamples =
      static_cast<int32_t>(sizeof(convertedBuffer_) / sizeof(int16_t));
  if (totalSamplesWide <= 0 || totalSamplesWide > maxSamples) {
    Trace::Error("WAVFILE: Requested buffer too large (%ld frames)", size);
    return false;
  }
  const int32_t totalSamples = static_cast<int32_t>(totalSamplesWide);
  samples_ = convertedBuffer_;
  std::fill_n(convertedBuffer_, totalSamples, int16_t{0});

  const int32_t bytesPerFrame = channelCount_ * bytePerSample_;
  const int32_t maxFramesPerRead =
      (bytesPerFrame > 0) ? (BUFFER_SIZE / bytesPerFrame) : 0;
  if (maxFramesPerRead == 0) {
    Trace::Error("WAVFILE: Invalid frame sizing");
    return false;
  }

  int32_t bufferStart = dataPosition_ + start * bytesPerFrame;
  int32_t framesRemaining =
      std::min<int32_t>(static_cast<int32_t>(size), size_ - start);
  int32_t dstOffset = 0;

  while (framesRemaining > 0) {
    const int32_t framesThisRead =
        std::min<int32_t>(framesRemaining, maxFramesPerRead);
    const int32_t readSize = framesThisRead * bytesPerFrame;

    const long bytesRead = readBlock(bufferStart, readSize);
    if (bytesRead != readSize) {
      Trace::Error("WAVFILE: Short read at frame %ld (%ld/%d bytes)",
                   start + dstOffset / channelCount_, bytesRead, readSize);
      return false;
    }

    for (int32_t i = 0; i < framesThisRead * channelCount_; ++i) {
      const uint8_t *samplePtr = readBuffer_ + i * bytePerSample_;
      convertedBuffer_[dstOffset + i] =
          ConvertSampleToInt16(samplePtr, audioFormat_, bytePerSample_);
    }
    bufferStart += readSize;
    framesRemaining -= framesThisRead;
    dstOffset += framesThisRead * channelCount_;
  }
  return true;
};

uint32_t WavFile::GetDiskSize(int note) { return sampleBufferSize_; }

// rewind to start of data (no header)
bool WavFile::Rewind() {
  if (!file_)
    return false;
  file_->Seek(dataPosition_, SEEK_SET);
  if (file_->Tell() != dataPosition_)
    return false;
  readCount_ = size_ * channelCount_ * bytePerSample_;
  return true;
};

// Both output formats share frame bounds and I/O failure handling. Decode from
// a bounded local block so a single PCM16 output frame can accept 24/32/64-bit
// input without needing room for the source representation in the destination.
template <typename Sample>
bool WavFile::ReadSamples(Sample *buffer, uint32_t capacity,
                          uint32_t *samplesRead) {
  if (!samplesRead)
    return false;
  *samplesRead = 0;
  if (!file_ || !buffer || channelCount_ <= 0 || bytePerSample_ <= 0)
    return false;

  const uint32_t sourceFrameSize = channelCount_ * bytePerSample_;
  const uint32_t blockFrames = BUFFER_SIZE / sourceFrameSize;
  if (blockFrames == 0)
    return false;
  uint32_t remaining =
      std::min(capacity / channelCount_, readCount_ / sourceFrameSize);
  std::array<uint8_t, BUFFER_SIZE> block;
  while (remaining > 0) {
    const uint32_t frames = std::min(remaining, blockFrames);
    const uint32_t bytes = frames * sourceFrameSize;
    // A short read before the declared data end is an I/O failure. Never
    // convert a negative result to unsigned or claim a truncated import worked.
    const int actual = file_->Read(block.data(), static_cast<int>(bytes));
    if (actual != static_cast<int>(bytes) || file_->Error())
      return false;
    const uint32_t count = frames * channelCount_;
    for (uint32_t i = 0; i < count; ++i) {
      const uint8_t *sample = block.data() + i * bytePerSample_;
      if constexpr (std::is_same_v<Sample, float>)
        buffer[*samplesRead + i] =
            ConvertSampleToFloat(sample, audioFormat_, bytePerSample_);
      else
        buffer[*samplesRead + i] =
            ConvertSampleToInt16(sample, audioFormat_, bytePerSample_);
    }
    *samplesRead += count;
    readCount_ -= bytes;
    remaining -= frames;
  }
  return true;
}

bool WavFile::Read(void *buffer, uint32_t capacityBytes, uint32_t *bytesRead) {
  if (!bytesRead)
    return false;
  uint32_t samplesRead = 0;
  const bool result =
      ReadSamples(static_cast<int16_t *>(buffer),
                  capacityBytes / sizeof(int16_t), &samplesRead);
  *bytesRead = samplesRead * sizeof(int16_t);
  return result;
}

bool WavFile::ReadFloat(float *buffer, uint32_t capacity,
                        uint32_t *samplesRead) {
  return ReadSamples(buffer, capacity, samplesRead);
}

bool WavFile::IsOpen() const { return static_cast<bool>(file_); }

void WavFile::Close() {
  if (file_) {
    file_.reset();
  }
}

int WavFile::GetRootNote(int note) { return NOTE_C3; }
