/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _STATUS_H_
#define _STATUS_H_

#include "Foundation/T_Factory.h"

#include <cstddef>

class Status : public T_Factory<Status> {
public:
  // Status::Set historically formats into one 128-byte stack buffer before
  // dispatching to the installed sink. Expose that contract so non-visual
  // sinks can preserve exactly the same truncation and storage semantics.
  static constexpr std::size_t kTextCapacity = 128U;

  virtual void Print(char *) = 0;
  virtual void PrintMultiLine(char *) = 0;
  static void Set(const char *fmt, ...);
  static void SetMultiLine(const char *fmt, ...);
};

#endif
