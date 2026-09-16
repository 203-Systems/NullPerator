/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 nILS Podewski
 * Copyright (c) 2026 NullPerator contributors
 * Adapted from copingTracker; see Externals/copingSynth/UPSTREAM.txt.
 */
#pragma once
#include "Application/Model/Song.h"
#include "Externals/copingSynth/StackInstrument/StackEngine.h"
#include "Externals/etl/include/etl/vector.h"
#include "I_Instrument.h"
#include "TrackVoices.h"
#include <array>

class StackInstrument final : public I_Instrument {
public:
  explicit StackInstrument(TrackVoicePool<stack_voice_t> *voices = nullptr);
  bool Init() override { return voices_.IsValid(); }
  bool IsInitialized() override { return voices_.IsValid(); }
  bool IsEmpty() override { return false; }
  InstrumentType GetType() override { return IT_STACK; }
  bool Start(int channel, unsigned char note, bool retrigger = true) override;
  void Stop(int channel) override;
  void OnStart() override;
  bool Render(int channel, fixed *buffer, int size, bool updateTick) override;
  void ProcessCommand(int channel, FourCC command, ushort value) override;
  int GetTable() override { return parameters_[3].GetInt(); }
  bool GetTableAutomation() override { return parameters_[4].GetBool(); }
  void GetTableState(TableSaveState &state) override { state = tableState_; }
  void SetTableState(TableSaveState &state) override { tableState_ = state; }
  etl::ivector<Variable *> *Variables() override { return &variables_; }

private:
  TableSaveState tableState_{};
  etl::vector<Variable *, 13> variables_;
  std::array<Variable, 13> parameters_;
  TrackVoices<stack_voice_t> voices_;
};
