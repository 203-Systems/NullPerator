/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Adapters/node/ui2/NodeUi2ButtonMap.h"

#include "doctest/doctest.h"

namespace {

using node::ui2::ActionForPhysicalButton;
using node::ui2::PhysicalButton;

} // namespace

TEST_CASE("Node UI2 physical directions preserve tracker direction order") {
  CHECK(ActionForPhysicalButton(PhysicalButton::Left) == TrackerAction::Left);
  CHECK(ActionForPhysicalButton(PhysicalButton::Down) == TrackerAction::Down);
  CHECK(ActionForPhysicalButton(PhysicalButton::Right) ==
        TrackerAction::Right);
  CHECK(ActionForPhysicalButton(PhysicalButton::Up) == TrackerAction::Up);
}

TEST_CASE("Node UI2 physical actions use the M8-style semantic mapping") {
  CHECK(ActionForPhysicalButton(PhysicalButton::Start) ==
        TrackerAction::Shift);
  CHECK(ActionForPhysicalButton(PhysicalButton::Select) ==
        TrackerAction::Play);
  CHECK(ActionForPhysicalButton(PhysicalButton::B) == TrackerAction::Option);
  CHECK(ActionForPhysicalButton(PhysicalButton::A) == TrackerAction::Edit);
  CHECK(ActionForPhysicalButton(PhysicalButton::Func) == TrackerAction::Power);
}

TEST_CASE("Node UI2 unknown physical buttons do not become valid actions") {
  constexpr auto unknown = static_cast<PhysicalButton>(0xFFU);
  CHECK(ActionForPhysicalButton(unknown) == TrackerAction::Count);
}
