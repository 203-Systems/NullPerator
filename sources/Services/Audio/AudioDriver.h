/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _AUDIO_DRIVER_H_
#define _AUDIO_DRIVER_H_

#include "AudioSettings.h"
#include "Foundation/Observable.h"

#define MAX_SAMPLE_COUNT 1875

class AudioDriver : public Observable {

public:
  class Event : public I_ObservableData {
  public:
    enum Type { ADET_DRIVERTICK, ADET_BUFFERNEEDED };

    Event(Type type) { type_ = type; };
    Type type_;
  };

public:
  AudioDriver(AudioSettings &settings);
  virtual ~AudioDriver();

  virtual bool Init();
  virtual void Close();
  virtual bool Start();
  virtual void Stop();

  virtual bool InitDriver() = 0;
  virtual void CloseDriver() = 0;
  virtual bool StartDriver() = 0;
  virtual void StopDriver() = 0;

  virtual bool Interlaced() = 0;
  virtual int GetPlayedBufferPercentage() = 0;
  virtual void OnAudioActive(bool) {}

  virtual double GetStreamTime() = 0; // in secs

  virtual void AddBuffer(short *buffer, int size) = 0; // size in samples

  AudioSettings GetAudioSettings();

  void OnNewBufferNeeded();

protected:
  void onAudioBufferTick();
  AudioSettings settings_;
};
#endif
