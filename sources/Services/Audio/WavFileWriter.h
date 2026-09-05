/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _WAV_FILE_WRITER_H_
#define _WAV_FILE_WRITER_H_

#include "Foundation/Types/Fixed.h"
#include "System/FileSystem/FileSystem.h"
#include <cstddef>
#include <cstdint>

class WavFileWriter {
public:
  WavFileWriter();
  WavFileWriter(const char *path);
  ~WavFileWriter();
  bool Open(const char *path);
  bool Open(FileHandle file);
  bool IsOpen() const;
  bool AddBuffer(const fixed *, int frames); // interleaved stereo frames
  bool Close();
  bool Failed() const { return failed_; }

private:
  static constexpr int kWriteChunkFrames = 256;
  std::uint32_t sampleCount_;
  bool failed_ = false;
  // Recording converts and writes in bounded chunks instead of permanently
  // reserving enough internal SRAM for the largest possible mixer callback.
  __attribute__((aligned(32))) static short buffer_[kWriteChunkFrames * 2];
  FileHandle file_;
};
#endif
