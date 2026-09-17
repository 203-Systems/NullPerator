/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SYSTEM_H_
#define _SYSTEM_H_

#include "Foundation/T_Factory.h"
#include "typedefs.h"
#include <stdint.h>
#include <stdlib.h>

struct BatteryState {
  uint8_t percentage;
  uint16_t voltage_mv;   // millivolts
  int16_t temperature_c; // celsius
  bool charging;
  bool error; // error: no available state for battery
};

enum class SampleImportStatus : uint32_t {
  Pending,
  Imported,
  Cancelled,
  Failed
};
struct SampleImportResult {
  char path[256]{};
  SampleImportStatus status = SampleImportStatus::Pending;
};

class System : public T_Factory<System> {

public: // Override in implementation
  virtual bool CanImportSample() const { return false; }
  virtual bool RequestSampleImport(const char *projectName) { return false; }
  virtual SampleImportResult PollSampleImport() { return {}; }
  virtual unsigned long GetClock() = 0; // millisecs
  virtual void GetBatteryState(BatteryState &state) = 0;
  virtual void SetDisplayBrightness(unsigned char value) = 0;
  virtual void PostQuitMessage() = 0;
  virtual unsigned int GetMemoryUsage() = 0;
  virtual void PowerDown() = 0;
  virtual void SystemPutChar(int c) = 0;
  virtual void SystemBootloader() = 0;
  virtual void SystemReboot() = 0;
  virtual uint32_t GetRandomNumber() = 0;
  virtual uint32_t Micros() = 0;
  virtual uint32_t Millis() = 0;
};

#endif
