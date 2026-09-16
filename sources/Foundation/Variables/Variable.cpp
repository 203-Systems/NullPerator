/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Variable.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"
#include <System/Console/nanoprintf.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Variable::~Variable(){};

Variable::Type Variable::GetType() { return descriptor_->type; };

FourCC Variable::GetID() { return descriptor_->id; };

const char *Variable::GetName() { return FourCC(descriptor_->id).c_str(); };

void Variable::SetFloat(float value, bool notify) {
  switch (descriptor_->type) {
  case FLOAT:
    value_.float_ = value;
    break;
  case INT:
    value_.int_ = int(value);
    break;
  case BOOL:
    value_.bool_ = bool(value != 0);
    break;
  case CHAR_LIST:
    value_.index_ = int(value);
    break;
  case STRING:
    char buf[10];
    npf_snprintf(buf, sizeof(buf), "%f", value);
    setStringValue(buf);
    break;
  };
  if (notify) {
    onChange();
  }
};

void Variable::SetInt(int value, bool notify) {
  switch (descriptor_->type) {
  case FLOAT:
    value_.float_ = float(value);
    break;
  case INT:
    value_.int_ = value;
    break;
  case BOOL:
    value_.bool_ = bool(value != 0);
    break;
  case CHAR_LIST:
    value_.index_ = value;
    break;
  case STRING:
    char buf[10];
    npf_snprintf(buf, sizeof(buf), "%d", value);
    setStringValue(buf);
    break;
  };
  if (notify) {
    onChange();
  }
};

void Variable::SetBool(bool value, bool notify) {
  switch (descriptor_->type) {
  case FLOAT:
    value_.float_ = float(value);
    break;
  case INT:
    value_.int_ = int(value);
    break;
  case BOOL:
    value_.bool_ = value;
    break;
  case CHAR_LIST:
    value_.index_ = int(value);
    break;
  case STRING:
    setStringValue(value ? "true" : "false");
    break;
  };
  if (notify) {
    onChange();
  }
};

float Variable::GetFloat() {
  switch (descriptor_->type) {
  case FLOAT:
    return value_.float_;
  case INT:
    return float(value_.int_);
  case BOOL:
    return float(value_.bool_);
  case CHAR_LIST:
    return float(value_.index_);
  case STRING:
    return float(atof(stringValue_->c_str()));
  };
  return 0.0f;
};

int Variable::GetInt() {
  switch (descriptor_->type) {
  case FLOAT:
    return int(value_.float_);
  case INT:
    return value_.int_;
  case BOOL:
    return int(value_.bool_);
  case CHAR_LIST:
    return value_.index_;
  case STRING:
    return atoi(stringValue_->c_str());
  };
  return 0;
};

bool Variable::GetBool() {
  switch (descriptor_->type) {
  case FLOAT:
    return bool(value_.float_ != 0);
  case INT:
    return bool(value_.int_ != 0);
  case BOOL:
    return value_.bool_;
  case CHAR_LIST:
    return bool(value_.index_ != 0);
  case STRING:
    return false;
  };
  return false;
};

void Variable::SetString(const char *string, bool notify) {
  NAssert(string);
  switch (descriptor_->type) {
  case FLOAT:
    value_.float_ = float(atof(string));
    break;
  case INT:
    value_.int_ = atoi(string);
    break;
  case BOOL:
    value_.bool_ = (!strcmp("false", string) ? false : true);
    break;
  case STRING:
    setStringValue(string);
    break;
  case CHAR_LIST:
    value_.index_ = -1;
    for (int i = 0; i < descriptor_->listSize; i++) {
      if (descriptor_->list[i]) {
        if (strcasecmp(string, descriptor_->list[i]) == 0) {
          value_.index_ = i;
          break;
        }
      }
    };
    break;
  };
  if (notify) {
    onChange();
  }
};

etl::string<MAX_VARIABLE_STRING_LENGTH> Variable::GetString() {
  char buf[MAX_VARIABLE_STRING_LENGTH];
  switch (descriptor_->type) {
  // !!! NOTE !!! we don't want to enable nanoprintf's float support so we just
  // cast to int here because we don't really display floats anyway
  case FLOAT:
    npf_snprintf(buf, sizeof(buf), "%d", static_cast<int>(value_.float_));
    break;
  case INT:
    npf_snprintf(buf, sizeof(buf), "%d", value_.int_);
    break;
  case BOOL:
    npf_snprintf(buf, sizeof(buf), "%s", value_.bool_ ? "true" : "false");
    break;
  case STRING:
    if (stringValue_) {
      return etl::string<MAX_VARIABLE_STRING_LENGTH>(stringValue_->c_str());
    }
    return "";
  case CHAR_LIST:
    if ((value_.index_ < 0) || (value_.index_ >= descriptor_->listSize)) {
      return "";
    } else {
      return descriptor_->list[value_.index_];
    }
    break;
  };

  return etl::string<MAX_VARIABLE_STRING_LENGTH>(buf, etl::strlen(buf));
};

void Variable::CopyFrom(Variable &other) {
  // Copy values, not identity/defaults or another object's mutable metadata.
  switch (GetType()) {
  case STRING:
    SetString(other.GetString().c_str());
    break;
  case FLOAT:
    SetFloat(other.GetFloat());
    break;
  case BOOL:
    SetBool(other.GetBool());
    break;
  case CHAR_LIST:
    SetString(other.GetString().c_str());
    break;
  case INT:
    SetInt(other.GetInt());
    break;
  }
}

const char *const *Variable::GetListPointer() {
  NAssert(descriptor_->type == CHAR_LIST);
  return descriptor_->list;
};

uint8_t Variable::GetListSize() {
  NAssert(descriptor_->type == CHAR_LIST);
  return descriptor_->listSize;
};

void Variable::Reset() {

  switch (descriptor_->type) {

  case FLOAT:
    value_.float_ = descriptor_->initial.float_;
    break;
  case INT:
    value_.int_ = descriptor_->initial.int_;
    break;
  case BOOL:
    value_.bool_ = descriptor_->initial.bool_;
    break;
  case CHAR_LIST:
    value_.index_ = descriptor_->initial.index_;
    break;
  case STRING:
    // TODO: Check if this may be needed in the future, not used at this time
    setStringValue("");
    break;
  }
  onChange();
}

void Variable::setStringValue(const char *value) {
  NAssert(stringValue_ != nullptr);
  stringValue_->assign(value);
}

bool Variable::IsModified() {
  switch (descriptor_->type) {
  case FLOAT:
    return value_.float_ != descriptor_->initial.float_;
  case INT:
    return value_.int_ != descriptor_->initial.int_;
  case BOOL:
    return value_.bool_ != descriptor_->initial.bool_;
  case CHAR_LIST:
    return value_.index_ != descriptor_->initial.index_;
  case STRING:
    // For string types, just compare against empty string
    return (stringValue_ != nullptr) && (stringValue_->size() > 0);
  }
  return false;
}
