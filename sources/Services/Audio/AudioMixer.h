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

#include "AudioModule.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/vector.h"
#include "Services/Audio/AudioDriver.h" // for MAX_SAMPLE_COUNT
#include "Services/Audio/WavFileWriter.h"
#include "config/MemorySections.h"
#include "config/StringLimits.h"

#include <atomic>
#include <cstdint>

class AudioMixer : public AudioModule {
public:
  // The graph's render worker owns the workspace. A mixer acquires it only
  // after its first audible input has completed, and releases on return.
  // A first-child mixer can therefore share it with its parent. Later children
  // must use independent workspaces; conflicting reuse rejects that render.
  struct Workspace {
    alignas(32) fixed temporary[MAX_SAMPLE_COUNT * 2];
    // One biased four-bit carry per channel. Ten full-width inputs need only
    // -5..+5, so both stereo carries fit in one byte without losing Q15 bits.
    std::uint8_t carry[MAX_SAMPLE_COUNT];

  private:
    friend class AudioMixer;
    bool inUse_ = false;
    bool conflict_ = false;
  };
  AudioMixer(const char *name, Workspace *workspace = nullptr);
  bool SetWorkspace(Workspace &workspace);

  virtual ~AudioMixer();
  virtual bool Render(fixed *buffer, int samplecount);
  void SetFileRenderer(const char *path);
  void EnableRendering(bool enable);
  [[nodiscard]] bool IsRendering() const {
    return enableRendering_ && writer_.IsOpen();
  }
  bool RenderFailed() const { return renderFailed_.load(); }
  void ClearRenderError() { renderFailed_.store(false); }
  void SetVolume(fixed volume);
  void SetName(etl::string<12> name) { name_ = name; };

  stereosample GetMixerLevels() {
    return peakMixerLevel_.load(std::memory_order_relaxed);
  }
  bool AddModule(AudioModule &module);
  void RemoveModule(AudioModule &module);
  void ClearModules();

private:
  bool enableRendering_;
  std::atomic<bool> renderFailed_{false};
  etl::string<STRING_AUDIO_RENDER_PATH_MAX> renderPath_;
  WavFileWriter writer_;
  fixed volume_;
  etl::string<12> name_;
  static constexpr size_t MaxModules = 10;
  static_assert(MaxModules <= 14, "stereo carry nibbles must not overflow");
  etl::vector<AudioModule *, MaxModules> modules_;

  // hold the avg volume of a buffer worth of samples for each audiomodule in
  // the mix
  // Audio rendering publishes meters from its worker task while UI2 samples
  // them on the application task. This packed scalar needs atomicity but does
  // not publish any dependent state, so relaxed ordering avoids a hot-path
  // memory barrier.
  std::atomic<stereosample> peakMixerLevel_{0};

  Workspace *workspace_;
};
#endif
