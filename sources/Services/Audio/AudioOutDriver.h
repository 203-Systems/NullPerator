/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _AUDIO_OUT_DRIVER_H_
#define _AUDIO_OUT_DRIVER_H_

#include "Application/Instruments/WavFileWriter.h"
#include "AudioDriver.h"
#include "AudioOut.h"
#include "Externals/etl/include/etl/string.h"
#include "Foundation/Observable.h"
#include "config/MemorySections.h"
#include "config/StringLimits.h"

class AudioDriver;

class AudioOutDriver : public AudioOut, protected I_Observer {
public:
  AudioOutDriver(AudioDriver &);
  ~AudioOutDriver() override;

  bool Init() override;
  void Close() override;
  bool Start() override;
  void Stop() override;
  void SetAudioActive(bool active) override;

  void Trigger() override;

  virtual stereosample GetLastPeakLevels();

  int GetPlayedBufferPercentage() override;

  AudioDriver *GetDriver();

  etl::string<STRING_AUDIO_API_MAX> GetAudioAPI() override;
  etl::string<STRING_AUDIO_DEVICE_MAX> GetAudioDevice() override;
  int GetAudioBufferSize() override;
  int GetAudioRequestedBufferSize() override;
  int GetAudioPreBufferCount() override;
  double GetStreamTime() override;

protected:
  void Update(Observable &o, I_ObservableData *d) override;

  void prepareMixBuffers();
  void clipToMix();

private:
  AudioDriver *driver_;
  bool hasSound_ = false;
  stereosample lastPeakVolume_ = 0;

  PICOTRACKER_FAST_AUDIO_BUFFER static fixed
      primarySoundBuffer_[MIX_BUFFER_SIZE];
  PICOTRACKER_FAST_AUDIO_BUFFER static short mixBuffer_[MIX_BUFFER_SIZE];
  int sampleCount_;
};
#endif
