/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "AudioMixer.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

#ifdef PICOTRACKER_ENABLE_PROFILING
#include "System/Console/Profiler.h"
#endif

namespace {

std::uint32_t FixedMagnitude(fixed value) {
  const std::uint32_t bits = static_cast<std::uint32_t>(value);
  return value < 0 ? std::uint32_t{0} - bits : bits;
}

std::uint16_t PeakLevel(std::uint32_t magnitude) {
  return static_cast<std::uint16_t>(std::min<std::uint32_t>(
      magnitude >> FIXED_SHIFT, std::numeric_limits<std::uint16_t>::max()));
}

} // namespace

AudioMixer::AudioMixer(const char *name, Workspace *workspace)
    : enableRendering_(0), name_(name), modules_(), workspace_(workspace) {
  volume_ = (i2fp(1));
};

AudioMixer::~AudioMixer() {}

void AudioMixer::SetFileRenderer(const char *path) { renderPath_ = path; };

void AudioMixer::EnableRendering(bool enable) {
  if (enable == enableRendering_) {
    return;
  }

  if (enable) {
    if (!writer_.Open(renderPath_.c_str())) {
      Trace::Error("AUDIO_MIXER", "Failed to open render file: %s",
                   renderPath_.c_str());
      renderFailed_.store(true);
      enableRendering_ = false;
      return;
    }
  }

  enableRendering_ = enable;
  if (!enable) {
    if (!writer_.Close())
      renderFailed_.store(true);
  }
};

bool AudioMixer::Render(fixed *buffer, int samplecount) {
#ifdef PICOTRACKER_ENABLE_PROFILING
  PROFILE_SCOPE(TraceCategory::Mixer, TraceName::MixerRender);
#endif
  if (!buffer || samplecount <= 0 || samplecount > MAX_SAMPLE_COUNT)
    return false;
  // One render worker traverses the graph. Detect recursive scratch reuse
  // before a child can overwrite its parent's temporary buffer or carries.
  if (workspace_ && workspace_->inUse_) {
    workspace_->conflict_ = true;
    renderFailed_.store(true);
    peakMixerLevel_.store(0, std::memory_order_relaxed);
    return false;
  }
  if (workspace_)
    workspace_->conflict_ = false;
  struct WorkspaceLease {
    bool *inUse = nullptr;
    ~WorkspaceLease() {
      if (inUse)
        *inUse = false;
    }
  } lease;
  bool gotData = false;
  bool needsWideGain = false;
  std::uint32_t peakL = 0;
  std::uint32_t peakR = 0;
  const int count = samplecount * 2;
  for (auto *mod : modules_) {
    if (!gotData) {
      gotData = mod->Render(buffer, samplecount);
    } else {
      // The first child renders directly into the destination. Its scratch
      // lifetime has ended, so acquire only now and clear its old carries.
      if (!lease.inUse) {
        workspace_->inUse_ = true;
        lease.inUse = &workspace_->inUse_;
        std::memset(workspace_->carry, 0x88, samplecount);
      }
      if (mod->Render(workspace_->temporary, samplecount)) {
        for (int i = 0; i < count; ++i) {
          const fixed a = buffer[i], b = workspace_->temporary[i];
          // Sum low words with defined unsigned wrap, retaining signed carries.
          // Ten full-scale Q15 modules need only four additional bits. This
          // preserves cancellation and fractional precision without a 64-bit
          // sample buffer or saturating before the master volume is applied.
          const fixed sum = static_cast<fixed>(static_cast<std::uint32_t>(a) +
                                               static_cast<std::uint32_t>(b));
          if (((a ^ sum) & (b ^ sum)) < 0) {
            const int increment = (i & 1) ? 16 : 1;
            workspace_->carry[i >> 1] += a < 0 ? -increment : increment;
            needsWideGain = true;
          }
          buffer[i] = sum;
        }
      }
    }
    if (workspace_ && workspace_->conflict_) {
      renderFailed_.store(true);
      peakMixerLevel_.store(0, std::memory_order_relaxed);
      return false;
    }
  }

  if (gotData) {
    if (needsWideGain) {
      for (int i = 0; i < count; ++i) {
        const std::int64_t sum =
            std::int64_t(buffer[i]) +
            std::int64_t(((workspace_->carry[i >> 1] >> ((i & 1) * 4)) & 0x0f) -
                         8) *
                (INT64_C(1) << 32);
        const auto scaled =
            volume_ == FP_ONE ? sum : (sum * volume_) >> FIXED_SHIFT;
        buffer[i] = static_cast<fixed>(
            std::clamp<std::int64_t>(scaled, std::numeric_limits<fixed>::min(),
                                     std::numeric_limits<fixed>::max()));
      }
    } else if (volume_ != FP_ONE) {
      for (int i = 0; i < count; ++i)
        buffer[i] = fp_mul(buffer[i], volume_);
    }
    for (int i = 0; i < samplecount; i += 32) {
      peakL = std::max(peakL, FixedMagnitude(buffer[2 * i]));
      peakR = std::max(peakR, FixedMagnitude(buffer[2 * i + 1]));
    }
  }

  // Always update peakMixerLevel_ regardless of whether we got data
  // This ensures VU meters update properly in all scenarios
  const stereosample peakLevel = static_cast<stereosample>(PeakLevel(peakL))
                                     << 16U |
                                 static_cast<stereosample>(PeakLevel(peakR));
  peakMixerLevel_.store(peakLevel, std::memory_order_relaxed);

  if (enableRendering_ && writer_.IsOpen()) {
    if (!gotData) {
      memset(buffer, 0, samplecount * 2 * sizeof(fixed));
    };
    if (!writer_.AddBuffer(buffer, samplecount))
      renderFailed_.store(true);
  }
  return gotData;
};

void AudioMixer::SetVolume(fixed volume) {
  volume_ = std::clamp<fixed>(volume, 0, FP_ONE);
}

bool AudioMixer::SetWorkspace(Workspace &workspace) {
  if (modules_.size() > 1)
    return false;
  workspace_ = &workspace;
  return true;
}

bool AudioMixer::AddModule(AudioModule &module) {
  if (modules_.full() || (!modules_.empty() && !workspace_) ||
      &module == this) {
    Trace::Error("AUDIOMIXER", "Invalid graph or missing branch workspace");
    return false;
  }
  modules_.push_back(&module);
  return true;
}

void AudioMixer::RemoveModule(AudioModule &module) {
  for (auto it = modules_.begin(); it != modules_.end(); ++it) {
    if (*it == &module) {
      modules_.erase(it);
      return;
    }
  }
}

void AudioMixer::ClearModules() { modules_.clear(); }
