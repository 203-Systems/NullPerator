/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _AUDIO_MIXER_H_
#define _AUDIO_MIXER_H_

#include "Application/Instruments/WavFileWriter.h"
#include "AudioModule.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/vector.h"
#include "Services/Audio/AudioDriver.h" // for MAX_SAMPLE_COUNT
#include "config/MemorySections.h"
#include "config/StringLimits.h"

#include <atomic>

class AudioMixer : public AudioModule {
public:
  AudioMixer(const char *name);
  virtual ~AudioMixer();
  virtual bool Render(fixed *buffer, int samplecount);
  void SetFileRenderer(const char *path);
  void EnableRendering(bool enable);
  [[nodiscard]] bool IsRendering() const {
    return enableRendering_ && writer_.IsOpen();
  }
  void SetVolume(fixed volume);
  void SetName(etl::string<12> name) { name_ = name; };

  stereosample GetMixerLevels() {
    return peakMixerLevel_.load(std::memory_order_relaxed);
  }
  void AddModule(AudioModule &module);
  void RemoveModule(AudioModule &module);
  void ClearModules();

private:
  bool enableRendering_;
  etl::string<STRING_AUDIO_RENDER_PATH_MAX> renderPath_;
  WavFileWriter writer_;
  fixed volume_;
  etl::string<12> name_;
  static constexpr size_t MaxModules = 10;
  etl::vector<AudioModule *, MaxModules> modules_;

  // hold the avg volume of a buffer worth of samples for each audiomodule in
  // the mix
  // Audio rendering publishes meters from its worker task while UI2 samples
  // them on the application task. This packed scalar needs atomicity but does
  // not publish any dependent state, so relaxed ordering avoids a hot-path
  // memory barrier.
  std::atomic<stereosample> peakMixerLevel_{0};

  PICOTRACKER_FAST_AUDIO_BUFFER static fixed
      renderBuffer_[MAX_SAMPLE_COUNT * 2];
};
#endif
