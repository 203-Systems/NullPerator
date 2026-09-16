/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _VARIABLE_H_
#define _VARIABLE_H_

#include "Externals/etl/include/etl/string.h"
#include "Foundation/Types/Types.h"

#define VAR_OFF -1

static const int MAX_VARIABLE_STRING_LENGTH = 40;

class Variable {

public:
  enum Type { INT, FLOAT, BOOL, CHAR_LIST, STRING };

public:
  union Value {
    int int_;
    float float_;
    bool bool_;
    int index_;
    constexpr Value(int value) : int_(value) {}
    constexpr Value(float value) : float_(value) {}
    constexpr Value(bool value) : bool_(value) {}
  };

  // Immutable descriptions belong to the parameter schema, not each preset.
  struct Descriptor {
    FourCC id;
    Type type;
    Value initial;
    const char *const *list = nullptr;
    uint8_t listSize = 0;
    constexpr Descriptor(FourCC id, int value = 0)
        : id(id), type(INT), initial(value) {}
    constexpr Descriptor(FourCC id, float value)
        : id(id), type(FLOAT), initial(value) {}
    constexpr Descriptor(FourCC id, bool value)
        : id(id), type(BOOL), initial(value) {}
    constexpr Descriptor(FourCC id, const char *const *list, int size,
                         int index = -1)
        : id(id), type(CHAR_LIST), initial(index), list(list), listSize(size) {
      initial.index_ = index;
    }
    Descriptor(FourCC, const char *) = delete;
  };

  explicit Variable(const Descriptor &descriptor)
      : Variable(&descriptor, descriptor.initial) {}
  Variable(Descriptor &&) = delete; // Never retain a temporary schema.
  Variable(const Variable &) = delete;
  Variable &operator=(const Variable &) = delete;

  virtual ~Variable();

  FourCC GetID();
  const char *GetName();

  Type GetType();
  virtual void SetInt(int value, bool notify = true);
  int GetInt();
  void SetFloat(float value, bool notify = true);
  float GetFloat();
  virtual void SetString(const char *string, bool notify = true);
  virtual etl::string<MAX_VARIABLE_STRING_LENGTH> GetString();
  void SetBool(bool value, bool notify = true);
  bool GetBool();
  void CopyFrom(Variable &other);
  // Not very clean !
  uint8_t GetListSize();
  const char *const *GetListPointer();
  virtual void Reset();

  // Check if the current value differs from the default value
  virtual bool IsModified();

protected:
  virtual void onChange(){};
  void setStringValue(const char *value);

  Variable(const Descriptor *descriptor, Value value)
      : value_(value), descriptor_(descriptor) {}
  Value value_;
  const Descriptor *descriptor_;
  etl::istring *stringValue_ = nullptr;
};

// Runtime-defined variables (configuration, strings and mutable sample lists)
// retain an inline descriptor. Instrument presets use shared const schemas.
class OwnedVariable : public Variable {
public:
  explicit OwnedVariable(FourCC id, int value = 0)
      : OwnedVariable(Descriptor(id, value)) {}
  OwnedVariable(FourCC id, float value)
      : OwnedVariable(Descriptor(id, value)) {}
  OwnedVariable(FourCC id, bool value) : OwnedVariable(Descriptor(id, value)) {}
  OwnedVariable(FourCC id, const char *const *list, int size, int index = -1)
      : OwnedVariable(Descriptor(id, list, size, index)) {}
  OwnedVariable(FourCC, const char *) = delete;

protected:
  explicit OwnedVariable(Descriptor descriptor)
      : Variable(&ownedDescriptor_, descriptor.initial),
        ownedDescriptor_(descriptor) {}
  Descriptor ownedDescriptor_;
};
#endif
