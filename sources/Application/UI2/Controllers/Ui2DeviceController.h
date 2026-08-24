/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ui2 {

// Logical navigation order, independent of platform-specific row visibility.
enum class Ui2DeviceField : std::uint8_t {
  MidiDevice = 0,
  MidiSync,
  RemoteUi,
  Resampler,
  LineOut,
  Volume,
  Brightness,
  Theme,
  Font,
  UpdateFirmware,
  Count,
};

enum class Ui2DeviceCommandType : std::uint8_t {
  None,
  SetSelector,
  BrowseTheme,
  BrowseFont,
  UpdateFirmware,
};

enum class Ui2DeviceBottomKind : std::uint8_t {
  Hidden,
  Selector,
  Action,
};

struct Ui2DeviceCommand {
  Ui2DeviceCommandType type = Ui2DeviceCommandType::None;
  Ui2DeviceField field = Ui2DeviceField::MidiDevice;
  std::uint16_t value = 0;

  [[nodiscard]] constexpr bool HasValue() const {
    return type != Ui2DeviceCommandType::None;
  }
};

struct Ui2DeviceBottomState {
  Ui2DeviceBottomKind kind = Ui2DeviceBottomKind::Hidden;
  Ui2DeviceCommandType action = Ui2DeviceCommandType::None;
  std::uint16_t count = 0;
  std::uint16_t current = 0;
  bool wrap = false;
};

class Ui2DeviceController {
public:
  static constexpr std::size_t FieldCount =
      static_cast<std::size_t>(Ui2DeviceField::Count);
  static constexpr std::uint32_t AllFieldsMask =
      Ui2FixedListCursor<FieldCount>::AllEnabledMask;

  constexpr Ui2DeviceController(std::uint32_t visibleFields = AllFieldsMask,
                                Ui2DeviceField selected =
                                    Ui2DeviceField::MidiDevice,
                                std::uint8_t viewportRows = 7)
      : cursor_(FieldIndex(selected), visibleFields, viewportRows) {}

  [[nodiscard]] constexpr Ui2DeviceField SelectedField() const {
    return static_cast<Ui2DeviceField>(cursor_.Selected());
  }
  [[nodiscard]] constexpr std::uint8_t FirstVisibleOrdinal() const {
    return cursor_.FirstVisibleOrdinal();
  }
  [[nodiscard]] constexpr std::uint8_t SelectedOrdinal() const {
    return cursor_.SelectedOrdinal();
  }
  [[nodiscard]] constexpr std::uint32_t VisibleFields() const {
    return cursor_.EnabledMask();
  }
  [[nodiscard]] constexpr std::uint16_t HeldMask() const {
    return input_.Mask();
  }

  [[nodiscard]] constexpr Ui2SelectorState
  Selector(Ui2DeviceField field) const {
    const std::size_t index = FieldIndex(field);
    return index < selectors_.size() ? selectors_[index] : Ui2SelectorState{};
  }

  constexpr void SetVisibleFields(std::uint32_t visibleFields) {
    cursor_.SetEnabledMask(visibleFields);
  }

  constexpr void SetSelector(Ui2DeviceField field,
                             Ui2SelectorState selector) {
    const std::size_t index = FieldIndex(field);
    if (index < selectors_.size())
      selectors_[index] = selector;
  }

  [[nodiscard]] constexpr Ui2DeviceBottomState Bottom() const {
    const Ui2DeviceField field = SelectedField();
    if (IsSelectorField(field)) {
      const Ui2SelectorState selector = Selector(field);
      return {.kind = selector.Valid() ? Ui2DeviceBottomKind::Selector
                                      : Ui2DeviceBottomKind::Hidden,
              .action = Ui2DeviceCommandType::None,
              .count = selector.count,
              .current = selector.current,
              .wrap = selector.wrap};
    }
    const Ui2DeviceCommandType action = ActionFor(field);
    return {.kind = action == Ui2DeviceCommandType::None
                        ? Ui2DeviceBottomKind::Hidden
                        : Ui2DeviceBottomKind::Action,
            .action = action};
  }

  constexpr Ui2DeviceCommand Handle(TrackerAction action, bool pressed) {
    if (!input_.Update(action, pressed) || !pressed)
      return {};

    if (input_.AnyModifier()) {
      if (action == TrackerAction::Enter &&
          input_.Mask() == TrackerActionBit(TrackerAction::Enter)) {
        const Ui2DeviceCommandType type = ActionFor(SelectedField());
        return {.type = type, .field = SelectedField()};
      }
      return {};
    }

    if (action == TrackerAction::Up) {
      cursor_.MovePrevious();
      return {};
    }
    if (action == TrackerAction::Down) {
      cursor_.MoveNext();
      return {};
    }
    if (action == TrackerAction::Left || action == TrackerAction::Right) {
      const Ui2DeviceField field = SelectedField();
      if (!IsSelectorField(field))
        return {};
      Ui2SelectorState &selector = selectors_[FieldIndex(field)];
      if (!selector.Move(action == TrackerAction::Left ? -1 : 1))
        return {};
      return {.type = Ui2DeviceCommandType::SetSelector,
              .field = field,
              .value = selector.current};
    }
    return {};
  }

private:
  [[nodiscard]] static constexpr std::size_t
  FieldIndex(Ui2DeviceField field) {
    return static_cast<std::size_t>(field);
  }

  [[nodiscard]] static constexpr bool
  IsSelectorField(Ui2DeviceField field) {
    switch (field) {
    case Ui2DeviceField::MidiDevice:
    case Ui2DeviceField::MidiSync:
    case Ui2DeviceField::RemoteUi:
    case Ui2DeviceField::Resampler:
    case Ui2DeviceField::LineOut:
    case Ui2DeviceField::Volume:
    case Ui2DeviceField::Brightness:
      return true;
    case Ui2DeviceField::Theme:
    case Ui2DeviceField::Font:
    case Ui2DeviceField::UpdateFirmware:
    case Ui2DeviceField::Count:
      return false;
    }
    return false;
  }

  [[nodiscard]] static constexpr Ui2DeviceCommandType
  ActionFor(Ui2DeviceField field) {
    switch (field) {
    case Ui2DeviceField::Theme:
      return Ui2DeviceCommandType::BrowseTheme;
    case Ui2DeviceField::Font:
      return Ui2DeviceCommandType::BrowseFont;
    case Ui2DeviceField::UpdateFirmware:
      return Ui2DeviceCommandType::UpdateFirmware;
    case Ui2DeviceField::MidiDevice:
    case Ui2DeviceField::MidiSync:
    case Ui2DeviceField::RemoteUi:
    case Ui2DeviceField::Resampler:
    case Ui2DeviceField::LineOut:
    case Ui2DeviceField::Volume:
    case Ui2DeviceField::Brightness:
    case Ui2DeviceField::Count:
      return Ui2DeviceCommandType::None;
    }
    return Ui2DeviceCommandType::None;
  }

  Ui2FixedListCursor<FieldCount> cursor_{};
  Ui2ControllerInputState input_{};
  std::array<Ui2SelectorState, FieldCount> selectors_{};
};

static_assert(std::is_trivially_copyable_v<Ui2DeviceCommand>);
static_assert(std::is_trivially_copyable_v<Ui2DeviceBottomState>);
static_assert(std::is_trivially_copyable_v<Ui2DeviceController>);
static_assert(sizeof(Ui2DeviceController) <= 80U);

} // namespace ui2
