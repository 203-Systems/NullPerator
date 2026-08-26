/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "FirmwareLifecyclePlatformAdapter.h"

#include "Application/Persistency/PersistencyService.h"
#include "Services/Midi/MidiService.h"
#include "System/FileSystem/FileSystem.h"
#include "System/System/System.h"

bool FirmwareLifecyclePlatformAdapter::InitializeMidi() {
  MidiService *midi = MidiService::GetInstance();
  return midi != nullptr && midi->Init();
}

void FirmwareLifecyclePlatformAdapter::CloseMidi() {
  if (MidiService *midi = MidiService::GetInstance())
    midi->Close();
}

FirmwareBatterySample FirmwareLifecyclePlatformAdapter::ReadBattery() {
  System *system = System::GetInstance();
  if (system == nullptr)
    return {};
  BatteryState state{};
  system->GetBatteryState(state);
  return {.percentage = state.percentage,
          .available = !state.error,
          .charging = state.charging};
}

void FirmwareLifecyclePlatformAdapter::PowerDown() {
  if (System *system = System::GetInstance())
    system->PowerDown();
}

bool FirmwareLifecyclePlatformAdapter::CanPrepareForcedUntitled() const {
  return FileSystem::GetInstance() != nullptr &&
         PersistencyService::GetInstance() != nullptr;
}

bool FirmwareLifecyclePlatformAdapter::DeleteCurrentProjectMarker() {
  FileSystem *fileSystem = FileSystem::GetInstance();
  return fileSystem != nullptr && fileSystem->DeleteFile("/.current");
}

bool FirmwareLifecyclePlatformAdapter::PurgeUntitledProject() {
  PersistencyService *persistency = PersistencyService::GetInstance();
  return persistency != nullptr && persistency->PurgeUnnamedProject();
}
