/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Application/Model/Song.h"

#include <optional>

// Keep a delayed row's VOL with its pending note. Applying it on the row
// boundary would change the previous voice and be reset by the new note.
class PhraseNoteTrigger {
public:
  void Reset() {
    ticks_ = 0;
    volume_.reset();
  }

  void Schedule(uchar note, FourCC command1, ushort param1, FourCC command2,
                ushort param2) {
    Reset();
    unsigned delay = 0;
    if (command1 == FourCC::InstrumentCommandDelay)
      delay = param1 & 0x0F;
    if (command2 == FourCC::InstrumentCommandDelay)
      delay = param2 & 0x0F;
    ticks_ = delay + 1;

    // Effect-only rows and note-offs retain their existing row timing.
    if (delay == 0 || note > HIGHEST_NOTE)
      return;
    if (command1 == FourCC::InstrumentCommandVolume)
      volume_ = param1;
    if (command2 == FourCC::InstrumentCommandVolume)
      volume_ = param2;
  }

  [[nodiscard]] bool Defers(FourCC command) const {
    return ticks_ != 0 && volume_.has_value() &&
           command == FourCC::InstrumentCommandVolume;
  }

  template <typename StartNote, typename ApplyVolume>
  void Tick(StartNote startNote, ApplyVolume applyVolume) {
    if (ticks_ == 0 || --ticks_ != 0)
      return;
    const auto volume = volume_;
    Reset();
    startNote();
    if (volume)
      applyVolume(*volume);
  }

private:
  unsigned ticks_ = 0;
  std::optional<ushort> volume_;
};
