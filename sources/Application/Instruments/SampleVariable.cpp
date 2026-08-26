/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "SampleVariable.h"
#include "SamplePool.h"

SampleVariable::SampleVariable(FourCC id) : WatchedVariable(id, 0, 0, -1) {
  SamplePool *pool = SamplePool::GetInstance();
  if (pool != nullptr) {
    list_.char_ = pool->GetNameList();
    listSize_ = pool->GetNameListSize();
    pool->AddObserver(*this);
  }
};

SampleVariable::~SampleVariable() {
  SamplePool *pool = SamplePool::GetInstance();
  if (pool != nullptr)
    pool->RemoveObserver(*this);
};

void SampleVariable::SetInt(int value, bool notify) {
  binding_.Clear();
  Variable::SetInt(value, notify);
}

void SampleVariable::SetString(const char *string, bool notify) {
  // Resolve against the current fixed pool list without notifying observers
  // until the fallback name has been captured.
  Variable::SetString(string, false);
  binding_.Capture(string, GetInt());
  if (notify)
    onChange();
}

etl::string<MAX_VARIABLE_STRING_LENGTH> SampleVariable::GetString() {
  if (GetInt() < 0 && binding_.HasUnresolvedName())
    return binding_.UnresolvedName();
  return Variable::GetString();
}

void SampleVariable::Reset() {
  binding_.Clear();
  Variable::Reset();
}

void SampleVariable::Update(Observable &o, I_ObservableData *d) {
  SamplePoolEvent *e = (SamplePoolEvent *)d;
  // If a sample was removed, update our index and notify observers so
  // instruments retarget their sample pointer.
  if (e->type_ == SPET_DELETE) {
    int currentIndex = value_.index_;
    int newIndex = currentIndex;

    if (currentIndex == e->index_) {
      newIndex = -1; // sample deleted, clear selection
    } else if (currentIndex > e->index_) {
      newIndex = currentIndex - 1;
    }

    if (newIndex != currentIndex) {
      SetInt(newIndex); // triggers onChange/NotifyObservers
    }
  }
  // For inserts, just refresh list pointers below
  // indices remain valid since imports append at the end
  SamplePool *pool = (SamplePool *)&o;
  list_.char_ = pool->GetNameList();
  listSize_ = pool->GetNameListSize();

  // A WAV that was absent during project restore can be imported later. Reuse
  // normal name resolution so the instrument becomes playable without losing
  // the persisted binding in the interim.
  if (e->type_ == SPET_INSERT && binding_.HasUnresolvedName()) {
    const etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> unresolved(
        binding_.UnresolvedName());
    SetString(unresolved.c_str());
  }
};
