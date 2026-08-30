/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _VARIABLE_CONTAINER_H_
#define _VARIABLE_CONTAINER_H_

#include "Externals/etl/include/etl/vector.h"
#include "Variable.h"

class VariableContainer {
public:
  VariableContainer(etl::ivector<Variable *> *variables);
  virtual ~VariableContainer();
  Variable *FindVariable(FourCC id);
  Variable *FindVariable(const char *name);

private:
  etl::ivector<Variable *> *variables_;
};
#endif
