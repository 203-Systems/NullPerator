/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include "Externals/etl/include/etl/vector.h"
#include "GBEngine.h"
#include "I_Instrument.h"
#include "TrackVoices.h"

class GBInstrument : public I_Instrument {
public:
  bool Init() override { return voices_.IsValid(); }
  bool IsInitialized() override { return voices_.IsValid(); }
  bool IsEmpty() override { return false; }
  InstrumentType GetType() override { return type_; }
  bool Start(int channel, unsigned char note, bool retrigger = true) override;
  void Stop(int channel) override { voices_.Stop(channel); }
  void OnStart() override {
    voices_.Reset();
    tableState_.Reset();
  }
  bool Render(int channel, fixed *buffer, int size, bool updateTick) override;
  void ProcessCommand(int channel, FourCC command, ushort value) override;
  int GetTable() override { return parameters_[2].GetInt(); }
  bool GetTableAutomation() override { return parameters_[3].GetBool(); }
  void GetTableState(TableSaveState &state) override { state = tableState_; }
  void SetTableState(TableSaveState &state) override { tableState_ = state; }
  etl::ivector<Variable *> *Variables() override { return &variables_; }

protected:
  GBInstrument(InstrumentType type, TrackVoicePool<gb::Voice> *voices);
  etl::vector<Variable *, 16> variables_;

private:
  InstrumentType type_;
  std::array<Variable, 8> parameters_;
  TableSaveState tableState_{};
  TrackVoices<gb::Voice> voices_;
};
class GBPulseInstrument final : public GBInstrument {
public:
  explicit GBPulseInstrument(TrackVoicePool<gb::Voice> *voices = nullptr)
      : GBInstrument(IT_GB_PULSE, voices) {}
};
class GBNoiseInstrument final : public GBInstrument {
public:
  explicit GBNoiseInstrument(TrackVoicePool<gb::Voice> *voices = nullptr)
      : GBInstrument(IT_GB_NOISE, voices) {}
};
class GBWaveInstrument final : public GBInstrument {
public:
  explicit GBWaveInstrument(TrackVoicePool<gb::Voice> *voices = nullptr);

private:
  std::array<Variable, 8> wave_;
};
