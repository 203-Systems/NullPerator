/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _PROJECT_H_
#define _PROJECT_H_

#include "Application/Instruments/InstrumentBank.h"
#include "Application/Model/ProjectDefaults.h"
#include "Application/Model/ProjectVersion.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Persistency/Persistent.h"
#include "BuildNumber.h"
#include "Foundation/Observable.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Variables/StringVariable.h"
#include "Foundation/Variables/VariableContainer.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "Song.h"

// BUILD_COUNT define comes from BuildNumber.h

#define MAX_TAP 3

class Project : public Persistent, public VariableContainer, I_Observer {
public:
  Project(const char *name);
  ~Project();
  void Load(const char *name);
  void Purge();
  void PurgeInstruments();
  void PurgeSamples();

  Song song_;

  int GetMasterVolume();
  int GetChannelVolume(int channel);
  bool Wrap();
  void OnTempoTap();
  void NudgeTempo(int value);
  int GetScale();
  uint8_t GetScaleRoot();
  int GetTempo(); // Takes nudging into account
  int GetTranspose();
  void GetProjectName(char *name);
  void SetProjectName(const char *name);
  bool SampleInUse(etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> filename);

  void Trigger();

  // I_Observer
  virtual void Update(Observable &o, I_ObservableData *d);

  InstrumentBank *GetInstrumentBank();

  // Persistent
  virtual void SaveContent(tinyxml2::XMLPrinter *printer);
  virtual void RestoreContent(PersistencyDocument *doc);

private:
  etl::vector<Variable *, 16> variables_;

  InstrumentBank instrumentBank_;
  int tempoNudge_;
  unsigned long lastTap_[MAX_TAP];
  unsigned int tempoTapCount_;
  // variables
  WatchedVariable tempo_;
  OwnedVariable masterVolume_;
  // Individual channel volume variables instead of using an array
  // as initialization of such a large array causes in constructors causes stack
  // overflow issues
  OwnedVariable channelVolume1_;
  OwnedVariable channelVolume2_;
  OwnedVariable channelVolume3_;
  OwnedVariable channelVolume4_;
  OwnedVariable channelVolume5_;
  OwnedVariable channelVolume6_;
  OwnedVariable channelVolume7_;
  OwnedVariable channelVolume8_;
  OwnedVariable wrap_;
  OwnedVariable transpose_;
  OwnedVariable scale_;
  OwnedVariable scaleRoot_;
  StringWatchedVariable<MAX_PROJECT_NAME_LENGTH> projectName_;
  OwnedVariable previewVolume_;
};

#endif
