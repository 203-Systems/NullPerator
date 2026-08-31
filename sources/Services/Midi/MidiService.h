/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _MIDI_SERVICE_H_
#define _MIDI_SERVICE_H_

#include "Externals/etl/include/etl/vector.h"
#include "Foundation/Observable.h"
#include "Foundation/T_Factory.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "MidiInDevice.h"
#include "MidiOutDevice.h"
#include <cstdint>

#define MIDI_MAX_BUFFERS 20

class MidiService : public T_Factory<MidiService>, public I_Observer {

public:
  MidiService();
  virtual ~MidiService();

  bool Init();
  void Close();
  bool Start();
  void Stop();

  //! player notification
  void OnPlayerStart();
  void OnPlayerStop();
  void RegisterActiveChannel(uint8_t channel);

  //! Queues a MidiMessage to the current time chunk
  void QueueMessage(MidiMessage &);

  //! Time chunk trigger
  void Trigger();
  void AdvancePlayQueue();

  //! Flush current queue to the output
  void Flush();

protected:
  etl::vector<MidiInDevice *, 2> inList_;
  etl::vector<MidiOutDevice *, 2> outList_;

  virtual void Update(Observable &o, I_ObservableData *d);
  void onAudioTick();

  // start the selected midi device
  void startDevice();

  // stop the selected midi device
  void stopDevice();

  // Platform services may normalize the firmware's physical-device bitmask
  // to their own topology (for example one selected Web MIDI output).
  virtual void updateActiveDevicesList(unsigned short config);

private:
  void flushOutQueue();

private:
  uint16_t activeMidiChannelMask_;
  etl::vector<MidiOutDevice *, 2> activeOutDevices_;

  etl::array<etl::vector<MidiMessage, MIDI_MAX_MESG_QUEUE>, MIDI_MAX_BUFFERS>
      queues_;

  int currentPlayQueue_;
  int currentOutQueue_;

  bool sendSync_;
  WatchedVariable *midiDeviceVariable_;
  WatchedVariable *midiSyncVariable_;
};
#endif
