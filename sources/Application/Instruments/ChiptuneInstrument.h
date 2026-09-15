/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 nILS Podewski
 * Copyright (c) 2026 NullPerator contributors
 * Adapted from copingTracker; see Externals/copingSynth/UPSTREAM.txt.
 */
#pragma once
#include "Application/Model/Song.h"
#include "Externals/copingSynth/ChiptuneInstrument/ChiptuneEngine.h"
#include "Externals/etl/include/etl/vector.h"
#include "I_Instrument.h"
#include <array>

class ChiptuneInstrument final : public I_Instrument {
public:
  ChiptuneInstrument();
  bool Init() override { return true; }
  bool IsInitialized() override { return true; }
  bool IsEmpty() override { return false; }
  InstrumentType GetType() override { return IT_CHIPTUNE; }
  bool Start(int channel, unsigned char note, bool retrigger = true) override;
  void Stop(int channel) override;
  void OnStart() override;
  bool Render(int channel, fixed *buffer, int size, bool updateTick) override;
  void ProcessCommand(int channel, FourCC command, ushort value) override;
  int GetTable() override { return parameters_[12].GetInt(); }
  bool GetTableAutomation() override { return parameters_[13].GetBool(); }
  void GetTableState(TableSaveState &state) override { state = tableState_; }
  void SetTableState(TableSaveState &state) override { tableState_ = state; }
  etl::ivector<Variable *> *Variables() override { return &variables_; }

private:
  TableSaveState tableState_{};
  etl::vector<Variable *, 14> variables_;
  std::array<Variable, 14> parameters_;
  std::array<coping::chip::voice_t, SONG_CHANNEL_COUNT> voices_{};
};
