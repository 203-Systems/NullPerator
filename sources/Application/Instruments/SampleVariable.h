/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SAMPLE_VARIABLE_H_
#define _SAMPLE_VARIABLE_H_

#include "Foundation/Observable.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "SampleBindingState.h"

class SampleVariable : public WatchedVariable, public I_Observer {
public:
  SampleVariable(FourCC id);
  ~SampleVariable();

  void SetInt(int value, bool notify = true) override;
  void SetString(const char *string, bool notify = true) override;
  etl::string<MAX_VARIABLE_STRING_LENGTH> GetString() override;
  void Reset() override;
  [[nodiscard]] bool HasUnresolvedName() const {
    return binding_.HasUnresolvedName();
  }

protected:
  virtual void Update(Observable &o, I_ObservableData *d);

private:
  SampleBindingState binding_;
};
#endif
