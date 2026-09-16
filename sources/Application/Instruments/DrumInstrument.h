/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 nILS Podewski
 * Copyright (c) 2026 NullPerator contributors
 * Adapted from copingTracker; see Externals/copingSynth/UPSTREAM.txt.
 */
#pragma once
#include "Application/Model/Song.h"
#include "Externals/copingSynth/DrumInstrument/DrumEngine.h"
#include "Externals/etl/include/etl/vector.h"
#include "I_Instrument.h"
#include "TrackVoices.h"
#include <array>

class DrumInstrument final : public I_Instrument {
public:
  bool FormatNote(unsigned char note, char *text,
                  unsigned size) const override {
    if (note > HIGHEST_NOTE || size < 4)
      return false;
    const unsigned slot = note % 12 + 1;
    text[0] = 'D';
    text[1] = '0' + slot / 10;
    text[2] = '0' + slot % 10;
    text[3] = '\0';
    return true;
  }
  bool EditNote(unsigned char note, int direction, bool coarse,
                unsigned char &result) const override {
    if (note > HIGHEST_NOTE)
      return false;
    // Retain the stored octave for existing projects; only kit selection wraps.
    const int slot =
        ((note % 12 + direction * (coarse ? 10 : 1)) % 12 + 12) % 12;
    result = static_cast<unsigned char>((note / 12) * 12 + slot);
    return true;
  }
  explicit DrumInstrument(TrackVoicePool<drum_voice_t> *voices = nullptr);
  bool Init() override { return voices_.IsValid(); }
  bool IsInitialized() override { return voices_.IsValid(); }
  bool IsEmpty() override { return false; }
  InstrumentType GetType() override { return IT_DRUM; }
  bool Start(int channel, unsigned char note, bool retrigger = true) override;
  void Stop(int channel) override;
  void OnStart() override;
  bool Render(int channel, fixed *buffer, int size, bool updateTick) override;
  void ProcessCommand(int channel, FourCC command, ushort value) override;
  int GetTable() override { return VAR_OFF; }
  bool GetTableAutomation() override { return false; }
  void GetTableState(TableSaveState &) override {}
  void SetTableState(TableSaveState &) override {}
  etl::ivector<Variable *> *Variables() override { return &variables_; }

private:
  etl::vector<Variable *, 13> variables_;
  std::array<Variable, 13> parameters_;
  TrackVoices<drum_voice_t> voices_;
};
