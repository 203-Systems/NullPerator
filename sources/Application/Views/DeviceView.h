/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _DEVICE_VIEW_H_
#define _DEVICE_VIEW_H_

#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UISwatchField.h"
#include "FieldView.h"
#include "Foundation/Observable.h"
#include "ViewData.h"

#include <array>
#include <cstdint>

struct DeviceViewUi2Choice {
  const char *const *options = nullptr;
  std::uint8_t count = 0;
  std::uint8_t current = 0;
  bool wrap = false;

  [[nodiscard]] const char *Value() const {
    return options != nullptr && current < count ? options[current] : "";
  }
};

enum class DeviceViewUi2Focus : std::uint8_t {
  MidiDevice,
  MidiSync,
  LineOut,
  RemoteUi,
  Resampler,
  Brightness,
  Volume,
  Theme,
  UpdateFirmware,
  Unknown,
};

// Compact, allocation-free representation of all settings consumed by UI2.
// Option pointers refer to Config's static string tables.
struct DeviceViewUi2Snapshot {
  DeviceViewUi2Choice midiDevice{};
  DeviceViewUi2Choice midiSync{};
  DeviceViewUi2Choice lineOut{};
  DeviceViewUi2Choice remoteUi{};
  DeviceViewUi2Choice resampler{};
  DeviceViewUi2Choice font{};
  std::array<char, MAX_VARIABLE_STRING_LENGTH + 1> theme{};
  std::array<char, 32> version{};
  std::uint8_t brightness = 0;
  std::uint8_t volume = 0;
  DeviceViewUi2Focus focus = DeviceViewUi2Focus::Unknown;

  [[nodiscard]] DeviceViewUi2Choice FocusedChoice() const;
};

class DeviceView : public FieldView, public I_Observer {
public:
  DeviceView(GUIWindow &w, ViewData *data);
  virtual ~DeviceView();

  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int){};
  virtual void OnFocus(){};
  virtual bool ShouldDrawPlayTime() const override;
  void OnFocusLost() override;

  [[nodiscard]] DeviceViewUi2Snapshot SnapshotForUi2() const;

  // Observer for action callback

  void Update(Observable &, I_ObservableData *);

protected:
private:
  void addSwatchField(ColorDefinition color, GUIPoint position);

  etl::vector<UIIntVarField, 7> intVarField_;
  etl::vector<UIActionField, 2> actionField_;
  etl::vector<UIBigHexVarField, 16> bigHexVarField_;
  etl::vector<UISwatchField, 16> swatchField_;
  bool configDirty_ = false;
};
#endif
