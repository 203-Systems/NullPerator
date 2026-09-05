/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Services/Audio/WavFileWriter.h"
#include "Services/Audio/WavHeader.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <algorithm>
#include <cstring>
#include <limits>

short WavFileWriter::buffer_[WavFileWriter::kWriteChunkFrames * 2];

WavFileWriter::WavFileWriter() : sampleCount_(0) {}

WavFileWriter::WavFileWriter(const char *path) : sampleCount_(0) {
  Open(path);
};

bool WavFileWriter::Open(const char *path) {
  // Finalize before opening: the new path may refer to the current file.
  Close();
  auto *filesystem = FileSystem::GetInstance();
  return Open(filesystem && path ? filesystem->Open(path, "w+b")
                                 : FileHandle{});
}

bool WavFileWriter::Open(FileHandle file) {
  Close();
  sampleCount_ = 0;
  failed_ = false;
  file_ = std::move(file);
  if (!file_) {
    failed_ = true;
    return false;
  }
  // Use WavHeaderWriter to write the header
  if (!WavHeaderWriter::WriteHeader(file_.get(), 44100, 2, 16)) {
    Trace::Log("WAVWRITER", "Failed to write WAV header");
    file_.reset();
    failed_ = true;
    return false;
  }
  return true;
}

bool WavFileWriter::IsOpen() const { return static_cast<bool>(file_); }

WavFileWriter::~WavFileWriter() { Close(); }

bool WavFileWriter::AddBuffer(const fixed *bufferIn, int size) {

  if (failed_)
    return false;
  if (!file_ || bufferIn == nullptr || size < 0 ||
      static_cast<std::uint32_t>(size) >
          (std::numeric_limits<std::uint32_t>::max() - 44U) / 4U -
              sampleCount_) {
    failed_ = true;
    return false;
  }

  const fixed *p = bufferIn;
  const fixed f_32767 = i2fp(32767);
  const fixed f_m32768 = i2fp(-32768);
  int framesRemaining = size;

  while (framesRemaining > 0) {
    const int chunkFrames = std::min(framesRemaining, kWriteChunkFrames);
    const int chunkSamples = chunkFrames * 2;
    short *output = buffer_;

    for (int i = 0; i < chunkSamples; ++i) {
      fixed value = *p++;
      if (value > f_32767) {
        value = f_32767;
      } else if (value < f_m32768) {
        value = f_m32768;
      }
      *output++ = short(fp2i(value));
    }

    const int written = file_->Write(buffer_, sizeof(short), chunkSamples);
    if (written != chunkSamples || file_->Error() != 0) {
      failed_ = true;
      return false;
    }
    sampleCount_ += chunkFrames;
    framesRemaining -= chunkFrames;
  }
  return true;
};

bool WavFileWriter::Close() {
  if (!file_)
    return !failed_;
  // Do not turn a partial/failed write into a valid-looking successful file.
  if (!failed_ && !WavHeaderWriter::UpdateFileSize(file_.get(), sampleCount_))
    failed_ = true;
  if (!file_.Close())
    failed_ = true;
  return !failed_;
}
