/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "PlayerChannel.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Player/SyncMaster.h"

#ifdef __EMSCRIPTEN__
#include "Adapters/wasm/tracing/WasmProfiler.h"
#endif

PlayerChannel::PlayerChannel(int index) {
  index_ = index;
  instr_ = 0;
  mixBus_ = 0;
  busIndex_ = -1;
}

PlayerChannel::~PlayerChannel() {}

void PlayerChannel::StartInstrument(I_Instrument *instr, unsigned char note,
                                    bool trigger) {
  if (instr_) {
    StopInstrument();
  }
  if (instr->Start(
          index_, note,
          trigger)) { // note could be refused coz it's out of the keymap
    instr_ = instr;
  } else {
    instr_ = 0;
  };
};

void PlayerChannel::StopInstrument() {
  if (instr_) {
    instr_->Stop(index_);
    instr_ = 0;
  }
};

bool PlayerChannel::Render(fixed *buffer, int samplecount) {
  if (instr_) {
#ifdef __EMSCRIPTEN__
    WASM_TRACE_SCOPE(WasmTraceCategory::Instrument,
                     WasmTraceName::InstrumentRender);
#endif
    bool tableSlice = SyncMaster::GetInstance()->TableSlice();
    bool status = instr_->Render(index_, buffer, samplecount, tableSlice);
    return status && !muted_.load(std::memory_order_relaxed);
  } else {
    return false;
  }
};

I_Instrument *PlayerChannel::GetInstrument() { return instr_; };

void PlayerChannel::SetMute(bool muted) {
  muted_.store(muted, std::memory_order_relaxed);
}

bool PlayerChannel::IsMuted() {
  return muted_.load(std::memory_order_relaxed);
}

void PlayerChannel::SetMixBus(int i) {

  if (i == busIndex_)
    return;

  if (mixBus_) {
    mixBus_->RemoveModule(*this);
  }
  mixBus_ = MixerService::GetInstance()->GetMixBus(i);
  if (mixBus_) {
    mixBus_->AddModule(*this);
    busIndex_ = i;
  } else {
    busIndex_ = -1;
  }
};

void PlayerChannel::Reset() {
  if (mixBus_) {
    mixBus_->RemoveModule(*this);
    mixBus_ = nullptr;
  }
  muted_.store(false, std::memory_order_relaxed);
  busIndex_ = -1;
};
