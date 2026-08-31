/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/TrackerInput.h"

#include <cstdint>
#include <type_traits>

namespace ui2 {

// This is the Project page's interaction order, independent from both the
// legacy FieldView hierarchy and the UI2 renderer's scene representation.
enum class Ui2ProjectContentCursor : std::uint8_t {
  Name = 0,
  Tempo,
  Transpose,
  Scale,
  Root,
  SamplePool,
  Samples,
  Instruments,
  Render,
  Count,
};

enum class Ui2ProjectNameAction : std::uint8_t {
  New = 0,
  Load,
  Save,
  Rename,
  Count,
};

enum class Ui2ProjectRenderSelection : std::uint8_t {
  Mixdown = 0,
  Stems,
  Count,
};

enum class Ui2ProjectSampleAction : std::uint8_t {
  Browse = 0,
  RemoveUnused,
  Count,
};

enum class Ui2ProjectBottomKind : std::uint8_t {
  Hidden,
  NameActions,
  ValueSelector,
  SampleActions,
  CleanupAction,
  RenderSelector,
};

// Commands are values. The application layer can consume them later without
// the controller depending on a View, callback, or model singleton.
enum class Ui2ProjectCommandType : std::uint8_t {
  None,
  NewProject,
  LoadProject,
  SaveProject,
  RenameProject,
  BrowseSamplePool,
  RemoveUnusedSamples,
  RemoveUnusedInstruments,
  RenderMixdown,
  RenderStems,
  AdjustTempo,
  AdjustTranspose,
  AdjustScale,
  AdjustRoot,
};

struct Ui2ProjectCommand {
  Ui2ProjectCommandType type = Ui2ProjectCommandType::None;
  std::int16_t value = 0;

  [[nodiscard]] constexpr bool HasValue() const {
    return type != Ui2ProjectCommandType::None;
  }
};

struct Ui2ProjectBottomState {
  Ui2ProjectBottomKind kind = Ui2ProjectBottomKind::Hidden;
  Ui2ProjectCommandType selectedCommand = Ui2ProjectCommandType::None;
  std::uint8_t selectedIndex = 0;
  std::uint8_t optionCount = 0;
};

class Ui2ProjectController {
public:
  constexpr Ui2ProjectController() = default;

  constexpr Ui2ProjectController(Ui2ProjectContentCursor cursor,
                                 Ui2ProjectNameAction nameAction,
                                 Ui2ProjectRenderSelection renderSelection,
                                 Ui2ProjectSampleAction sampleAction =
                                     Ui2ProjectSampleAction::Browse)
      : cursor_(Sanitize(cursor)), nameAction_(Sanitize(nameAction)),
        renderSelection_(Sanitize(renderSelection)),
        sampleAction_(Sanitize(sampleAction)) {}

  [[nodiscard]] constexpr Ui2ProjectContentCursor ContentCursor() const {
    return cursor_;
  }

  [[nodiscard]] constexpr Ui2ProjectNameAction NameAction() const {
    return nameAction_;
  }

  [[nodiscard]] constexpr Ui2ProjectRenderSelection RenderSelection() const {
    return renderSelection_;
  }

  [[nodiscard]] constexpr Ui2ProjectSampleAction SampleAction() const {
    return sampleAction_;
  }

  constexpr void SetEnterHeld(bool held) { enterHeld_ = held; }
  [[nodiscard]] constexpr bool EnterHeld() const { return enterHeld_; }

  // Vertical movement only changes the content cursor. Contextual action
  // choices remain owned by the controller and survive leaving their row.
  constexpr void MoveUp() {
    const std::uint8_t index = CursorIndex();
    std::uint8_t previous = index == 0 ? ContentCount() - 1U : index - 1U;
    if (previous == static_cast<std::uint8_t>(
                        Ui2ProjectContentCursor::SamplePool))
      --previous;
    cursor_ = static_cast<Ui2ProjectContentCursor>(previous);
  }

  constexpr void MoveDown() {
    const std::uint8_t index = CursorIndex();
    std::uint8_t next = static_cast<std::uint8_t>((index + 1U) % ContentCount());
    if (next ==
        static_cast<std::uint8_t>(Ui2ProjectContentCursor::SamplePool))
      ++next;
    cursor_ = static_cast<Ui2ProjectContentCursor>(next);
  }

  constexpr void MoveLeft() {
    if (cursor_ == Ui2ProjectContentCursor::Name) {
      nameAction_ = static_cast<Ui2ProjectNameAction>(Previous(
          static_cast<std::uint8_t>(nameAction_), NameActionCount()));
    } else if (cursor_ == Ui2ProjectContentCursor::Render) {
      renderSelection_ = static_cast<Ui2ProjectRenderSelection>(Previous(
          static_cast<std::uint8_t>(renderSelection_), RenderOptionCount()));
    } else if (cursor_ == Ui2ProjectContentCursor::SamplePool ||
               cursor_ == Ui2ProjectContentCursor::Samples) {
      sampleAction_ = static_cast<Ui2ProjectSampleAction>(Previous(
          static_cast<std::uint8_t>(sampleAction_), SampleActionCount()));
    }
  }

  constexpr void MoveRight() {
    if (cursor_ == Ui2ProjectContentCursor::Name) {
      nameAction_ = static_cast<Ui2ProjectNameAction>(
          Next(static_cast<std::uint8_t>(nameAction_), NameActionCount()));
    } else if (cursor_ == Ui2ProjectContentCursor::Render) {
      renderSelection_ = static_cast<Ui2ProjectRenderSelection>(Next(
          static_cast<std::uint8_t>(renderSelection_), RenderOptionCount()));
    } else if (cursor_ == Ui2ProjectContentCursor::SamplePool ||
               cursor_ == Ui2ProjectContentCursor::Samples) {
      sampleAction_ = static_cast<Ui2ProjectSampleAction>(
          Next(static_cast<std::uint8_t>(sampleAction_), SampleActionCount()));
    }
  }

  [[nodiscard]] constexpr Ui2ProjectBottomState Bottom() const {
    switch (cursor_) {
    case Ui2ProjectContentCursor::Name:
      return {.kind = Ui2ProjectBottomKind::NameActions,
              .selectedCommand = NameCommand(nameAction_),
              .selectedIndex = static_cast<std::uint8_t>(nameAction_),
              .optionCount = NameActionCount()};
    case Ui2ProjectContentCursor::SamplePool:
    case Ui2ProjectContentCursor::Samples:
      return {.kind = Ui2ProjectBottomKind::SampleActions,
              .selectedCommand = SampleCommand(sampleAction_),
              .selectedIndex = static_cast<std::uint8_t>(sampleAction_),
              .optionCount = SampleActionCount()};
    case Ui2ProjectContentCursor::Instruments:
      return {.kind = Ui2ProjectBottomKind::CleanupAction,
              .selectedCommand =
                  Ui2ProjectCommandType::RemoveUnusedInstruments,
              .selectedIndex = 0,
              .optionCount = 1};
    case Ui2ProjectContentCursor::Render:
      return {.kind = Ui2ProjectBottomKind::RenderSelector,
              .selectedCommand = RenderCommand(renderSelection_),
              .selectedIndex = static_cast<std::uint8_t>(renderSelection_),
              .optionCount = RenderOptionCount()};
    case Ui2ProjectContentCursor::Tempo:
    case Ui2ProjectContentCursor::Transpose:
    case Ui2ProjectContentCursor::Scale:
    case Ui2ProjectContentCursor::Root:
      return {.kind = Ui2ProjectBottomKind::ValueSelector};
    case Ui2ProjectContentCursor::Count:
      return {};
    }
    return {};
  }

  [[nodiscard]] constexpr Ui2ProjectCommand Enter() const {
    return {.type = Bottom().selectedCommand};
  }

  [[nodiscard]] constexpr Ui2ProjectCommand
  Adjust(TrackerAction action) const {
    const bool decrease = action == TrackerAction::Left ||
                          action == TrackerAction::Down;
    if (action != TrackerAction::Left && action != TrackerAction::Right &&
        action != TrackerAction::Up && action != TrackerAction::Down)
      return {};
    const bool vertical =
        action == TrackerAction::Up || action == TrackerAction::Down;
    const std::int16_t sign = decrease ? -1 : 1;
    switch (cursor_) {
    case Ui2ProjectContentCursor::Tempo:
      return {.type = Ui2ProjectCommandType::AdjustTempo,
              .value = static_cast<std::int16_t>(sign * (vertical ? 10 : 1))};
    case Ui2ProjectContentCursor::Transpose:
      return {.type = Ui2ProjectCommandType::AdjustTranspose,
              .value = static_cast<std::int16_t>(sign * (vertical ? 12 : 1))};
    case Ui2ProjectContentCursor::Scale:
      return {.type = Ui2ProjectCommandType::AdjustScale,
              .value = static_cast<std::int16_t>(sign * (vertical ? 10 : 1))};
    case Ui2ProjectContentCursor::Root:
      return {.type = Ui2ProjectCommandType::AdjustRoot, .value = sign};
    case Ui2ProjectContentCursor::Name:
    case Ui2ProjectContentCursor::SamplePool:
    case Ui2ProjectContentCursor::Samples:
    case Ui2ProjectContentCursor::Instruments:
    case Ui2ProjectContentCursor::Render:
    case Ui2ProjectContentCursor::Count:
      return {};
    }
    return {};
  }

private:
  [[nodiscard]] static constexpr std::uint8_t ContentCount() {
    return static_cast<std::uint8_t>(Ui2ProjectContentCursor::Count);
  }

  [[nodiscard]] static constexpr std::uint8_t NameActionCount() {
    return static_cast<std::uint8_t>(Ui2ProjectNameAction::Count);
  }

  [[nodiscard]] static constexpr std::uint8_t RenderOptionCount() {
    return static_cast<std::uint8_t>(Ui2ProjectRenderSelection::Count);
  }

  [[nodiscard]] static constexpr std::uint8_t SampleActionCount() {
    return static_cast<std::uint8_t>(Ui2ProjectSampleAction::Count);
  }

  [[nodiscard]] constexpr std::uint8_t CursorIndex() const {
    const std::uint8_t index = static_cast<std::uint8_t>(cursor_);
    return index < ContentCount() ? index : 0;
  }

  [[nodiscard]] static constexpr std::uint8_t Next(std::uint8_t value,
                                                    std::uint8_t count) {
    return static_cast<std::uint8_t>((value + 1U) % count);
  }

  [[nodiscard]] static constexpr std::uint8_t Previous(std::uint8_t value,
                                                        std::uint8_t count) {
    return value == 0 ? static_cast<std::uint8_t>(count - 1U)
                      : static_cast<std::uint8_t>(value - 1U);
  }

  [[nodiscard]] static constexpr Ui2ProjectContentCursor
  Sanitize(Ui2ProjectContentCursor cursor) {
    return static_cast<std::uint8_t>(cursor) < ContentCount()
               ? cursor
               : Ui2ProjectContentCursor::Name;
  }

  [[nodiscard]] static constexpr Ui2ProjectNameAction
  Sanitize(Ui2ProjectNameAction action) {
    return static_cast<std::uint8_t>(action) < NameActionCount()
               ? action
               : Ui2ProjectNameAction::New;
  }

  [[nodiscard]] static constexpr Ui2ProjectRenderSelection
  Sanitize(Ui2ProjectRenderSelection selection) {
    return static_cast<std::uint8_t>(selection) < RenderOptionCount()
               ? selection
               : Ui2ProjectRenderSelection::Mixdown;
  }

  [[nodiscard]] static constexpr Ui2ProjectSampleAction
  Sanitize(Ui2ProjectSampleAction action) {
    return static_cast<std::uint8_t>(action) < SampleActionCount()
               ? action
               : Ui2ProjectSampleAction::Browse;
  }

  [[nodiscard]] static constexpr Ui2ProjectCommandType
  SampleCommand(Ui2ProjectSampleAction action) {
    return action == Ui2ProjectSampleAction::RemoveUnused
               ? Ui2ProjectCommandType::RemoveUnusedSamples
               : Ui2ProjectCommandType::BrowseSamplePool;
  }

  [[nodiscard]] static constexpr Ui2ProjectCommandType
  NameCommand(Ui2ProjectNameAction action) {
    switch (action) {
    case Ui2ProjectNameAction::New:
      return Ui2ProjectCommandType::NewProject;
    case Ui2ProjectNameAction::Save:
      return Ui2ProjectCommandType::SaveProject;
    case Ui2ProjectNameAction::Load:
      return Ui2ProjectCommandType::LoadProject;
    case Ui2ProjectNameAction::Rename:
      return Ui2ProjectCommandType::RenameProject;
    case Ui2ProjectNameAction::Count:
      return Ui2ProjectCommandType::None;
    }
    return Ui2ProjectCommandType::None;
  }

  [[nodiscard]] static constexpr Ui2ProjectCommandType
  RenderCommand(Ui2ProjectRenderSelection selection) {
    switch (selection) {
    case Ui2ProjectRenderSelection::Mixdown:
      return Ui2ProjectCommandType::RenderMixdown;
    case Ui2ProjectRenderSelection::Stems:
      return Ui2ProjectCommandType::RenderStems;
    case Ui2ProjectRenderSelection::Count:
      return Ui2ProjectCommandType::None;
    }
    return Ui2ProjectCommandType::None;
  }

  Ui2ProjectContentCursor cursor_ = Ui2ProjectContentCursor::Name;
  Ui2ProjectNameAction nameAction_ = Ui2ProjectNameAction::New;
  Ui2ProjectRenderSelection renderSelection_ =
      Ui2ProjectRenderSelection::Mixdown;
  Ui2ProjectSampleAction sampleAction_ = Ui2ProjectSampleAction::Browse;
  bool enterHeld_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2ProjectCommand>);
static_assert(std::is_trivially_copyable_v<Ui2ProjectBottomState>);
static_assert(std::is_trivially_copyable_v<Ui2ProjectController>);
static_assert(sizeof(Ui2ProjectController) <= 8U);

} // namespace ui2
