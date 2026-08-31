/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026
 *
 * This file is part of the picoTracker firmware
 */

#pragma once

#include <cstdint>

// Fixed-size ownership state for the output producer. Player serializes all
// mutations with the mixer lock, so this stays allocation-free and avoids an
// atomic/ref-count on the audio hot path.
class PlayerAudioActivity final {
public:
  static constexpr std::uint8_t VoiceCount = 8U;

  enum class Source : std::uint16_t {
    Transport = 1U << 0U,
    FileStream = 1U << 1U,
    RecordStream = 1U << 2U,
  };

  void Set(Source source, bool active) {
    SetMask(static_cast<std::uint16_t>(source), active);
  }

  void SetVoice(std::uint8_t voice, bool active) {
    if (voice >= VoiceCount)
      return;
    SetMask(static_cast<std::uint16_t>(VoiceMaskStart << voice), active);
  }

  void ClearVoices() { sources_ &= static_cast<std::uint16_t>(~VoiceMask); }
  void ClearTransport() {
    ClearVoices();
    Set(Source::Transport, false);
  }
  void Reset() { sources_ = 0U; }

  [[nodiscard]] bool IsActive() const { return sources_ != 0U; }

private:
  static constexpr std::uint16_t VoiceMaskStart = 1U << 3U;
  static constexpr std::uint16_t VoiceMask =
      static_cast<std::uint16_t>(((1U << VoiceCount) - 1U) << 3U);

  void SetMask(std::uint16_t mask, bool active) {
    if (active)
      sources_ |= mask;
    else
      sources_ &= static_cast<std::uint16_t>(~mask);
  }

  std::uint16_t sources_ = 0U;
};

static_assert(sizeof(PlayerAudioActivity) == sizeof(std::uint16_t),
              "audio ownership must stay embedded-friendly");
