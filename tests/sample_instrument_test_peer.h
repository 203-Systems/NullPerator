#pragma once

#include "Application/Instruments/SampleInstrument.h"

struct SampleInstrumentTestPeer {
  static void BindSource(SampleInstrument &instrument, SoundSource &source) {
    instrument.source_ = &source;
  }

  static renderParams &Params(int channel) {
    return SampleInstrument::renderParams_[channel];
  }
};
