/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Application/Model/Song.h"
#include <array>
#include <memory>
#include <new>

// Presets in a project share one engine state per track, not eight states per
// preset. Ownership checks keep a retired preset from stopping a newer voice.
template <typename Voice> struct TrackVoicePool {
  std::array<Voice, SONG_CHANNEL_COUNT> voices{};
  std::array<const void *, SONG_CHANNEL_COUNT> owners{};
};

template <typename Voice> class TrackVoices {
public:
  using Pool = TrackVoicePool<Voice>;
  explicit TrackVoices(Pool *pool = nullptr)
      : owned_(pool ? nullptr : new(std::nothrow) Pool),
        pool_(pool ? pool : owned_.get()) {}
  ~TrackVoices() { Reset(); }
  TrackVoices(const TrackVoices &) = delete;
  TrackVoices &operator=(const TrackVoices &) = delete;

  bool IsValid() const { return pool_ != nullptr; }
  bool Owns(int channel) const {
    return pool_ && channel >= 0 && channel < SONG_CHANNEL_COUNT &&
           pool_->owners[channel] == this;
  }
  Voice &Acquire(int channel) {
    if (!Owns(channel)) {
      pool_->voices[channel] = Voice{};
      pool_->owners[channel] = this;
    }
    return pool_->voices[channel];
  }
  Voice &operator[](int channel) { return pool_->voices[channel]; }
  void Stop(int channel) {
    if (Owns(channel)) {
      pool_->voices[channel].stop();
      pool_->owners[channel] = nullptr;
    }
  }
  void Reset() {
    for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel)
      Stop(channel);
  }

private:
  // Standalone instruments (DSP tools/tests) get an isolated pool. Production
  // always supplies the bank-owned pool; no allocation happens at note-on.
  std::unique_ptr<Pool> owned_;
  Pool *pool_;
};
