/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/Session/AutoSaveCoordinator.h"

#include <doctest/doctest.h>
#include <limits>
#include <type_traits>

namespace {

AutoSaveCoordinator::Conditions Eligible() {
  return {
      .projectLoaded = true,
      .playerRunning = false,
      .recordingActive = false,
      .operationAllowsSave = true,
  };
}

} // namespace

static_assert(std::is_trivially_destructible_v<AutoSaveCoordinator>);
static_assert(sizeof(AutoSaveCoordinator) <= 24U);

TEST_CASE("autosave requires dirty data and a full cadence") {
  AutoSaveCoordinator coordinator;
  coordinator.OnProjectLoaded(1'000U);

  CHECK(coordinator.Tick(61'000U, Eligible()) ==
        AutoSaveCoordinator::TickResult::Idle);
  CHECK_FALSE(coordinator.Dirty());

  coordinator.MarkDirty(62'000U);
  CHECK(coordinator.Tick(120'999U, Eligible()) ==
        AutoSaveCoordinator::TickResult::Idle);
  CHECK(coordinator.Tick(121'000U, Eligible()) ==
        AutoSaveCoordinator::TickResult::SaveRequested);
  CHECK(coordinator.PersistBusy());
  CHECK(coordinator.AutoSaveInFlight());
}

TEST_CASE("additional edits do not debounce the autosave deadline") {
  AutoSaveCoordinator coordinator;
  coordinator.OnProjectLoaded(100U);
  coordinator.MarkDirty(1'000U);
  coordinator.MarkDirty(59'999U);

  CHECK(coordinator.Tick(60'099U, Eligible()) ==
        AutoSaveCoordinator::TickResult::Idle);
  CHECK(coordinator.Tick(60'100U, Eligible()) ==
        AutoSaveCoordinator::TickResult::SaveRequested);
}

TEST_CASE("every unsafe lifecycle condition defers a due autosave") {
  using TickResult = AutoSaveCoordinator::TickResult;
  AutoSaveCoordinator coordinator;
  coordinator.OnProjectLoaded(0U);
  coordinator.MarkDirty(1U);

  SUBCASE("project unloaded") {
    auto conditions = Eligible();
    conditions.projectLoaded = false;
    CHECK(coordinator.Tick(60'000U, conditions) == TickResult::Deferred);
  }
  SUBCASE("player running") {
    auto conditions = Eligible();
    conditions.playerRunning = true;
    CHECK(coordinator.Tick(60'000U, conditions) == TickResult::Deferred);
  }
  SUBCASE("recording active") {
    auto conditions = Eligible();
    conditions.recordingActive = true;
    CHECK(coordinator.Tick(60'000U, conditions) == TickResult::Deferred);
  }
  SUBCASE("operation disallows save") {
    auto conditions = Eligible();
    conditions.operationAllowsSave = false;
    CHECK(coordinator.Tick(60'000U, conditions) == TickResult::Deferred);
  }
  SUBCASE("persistence busy") {
    coordinator.SetPersistBusy(true);
    CHECK(coordinator.Tick(60'000U, Eligible()) == TickResult::Deferred);
  }
  SUBCASE("Save As pending") {
    coordinator.SetSaveAsPending(true);
    CHECK(coordinator.Tick(60'000U, Eligible()) == TickResult::Deferred);
  }
}

TEST_CASE("a deferred autosave runs as soon as all guards become safe") {
  AutoSaveCoordinator coordinator;
  coordinator.OnProjectLoaded(0U);
  coordinator.MarkDirty(10U);
  auto conditions = Eligible();
  conditions.playerRunning = true;

  CHECK(coordinator.Tick(60'000U, conditions) ==
        AutoSaveCoordinator::TickResult::Deferred);
  conditions.playerRunning = false;
  CHECK(coordinator.Tick(61'000U, conditions) ==
        AutoSaveCoordinator::TickResult::SaveRequested);
}

TEST_CASE("successful autosave clears only the revision it wrote") {
  AutoSaveCoordinator coordinator;
  coordinator.OnProjectLoaded(0U);
  coordinator.MarkDirty(1U);
  CHECK(coordinator.Tick(60'000U, Eligible()) ==
        AutoSaveCoordinator::TickResult::SaveRequested);

  coordinator.MarkDirty(60'001U);
  coordinator.CompleteAutoSave(60'002U, true);
  CHECK(coordinator.Dirty());
  CHECK_FALSE(coordinator.PersistBusy());
  CHECK(coordinator.Tick(120'001U, Eligible()) ==
        AutoSaveCoordinator::TickResult::Idle);
  CHECK(coordinator.Tick(120'002U, Eligible()) ==
        AutoSaveCoordinator::TickResult::SaveRequested);
}

TEST_CASE("successful autosave clears dirty state and resets its deadline") {
  AutoSaveCoordinator coordinator;
  coordinator.OnProjectLoaded(5'000U);
  coordinator.MarkDirty(5'001U);
  CHECK(coordinator.Tick(65'000U, Eligible()) ==
        AutoSaveCoordinator::TickResult::SaveRequested);
  coordinator.CompleteAutoSave(65'010U, true);

  CHECK_FALSE(coordinator.Dirty());
  CHECK_FALSE(coordinator.PersistBusy());
  CHECK_FALSE(coordinator.Due(125'009U));
  CHECK(coordinator.Due(125'010U));
}

TEST_CASE("failed autosave stays dirty and retries on the next cadence") {
  AutoSaveCoordinator coordinator;
  coordinator.OnProjectLoaded(0U);
  coordinator.MarkDirty(1U);
  CHECK(coordinator.Tick(60'000U, Eligible()) ==
        AutoSaveCoordinator::TickResult::SaveRequested);
  coordinator.CompleteAutoSave(60'000U, false);

  CHECK(coordinator.Dirty());
  CHECK(coordinator.Tick(119'999U, Eligible()) ==
        AutoSaveCoordinator::TickResult::Idle);
  CHECK(coordinator.Tick(120'000U, Eligible()) ==
        AutoSaveCoordinator::TickResult::SaveRequested);
}

TEST_CASE("project lifecycle resets dirty deadline busy and Save As state") {
  using Hook = void (AutoSaveCoordinator::*)(std::uint32_t) noexcept;
  constexpr Hook hooks[] = {
      &AutoSaveCoordinator::OnProjectLoaded,
      &AutoSaveCoordinator::OnProjectCreated,
      &AutoSaveCoordinator::OnProjectSaved,
  };

  for (Hook hook : hooks) {
    AutoSaveCoordinator coordinator;
    coordinator.OnProjectLoaded(0U);
    coordinator.MarkDirty(1U);
    coordinator.SetPersistBusy(true);
    coordinator.SetSaveAsPending(true);

    (coordinator.*hook)(10'000U);
    CHECK_FALSE(coordinator.Dirty());
    CHECK_FALSE(coordinator.PersistBusy());
    CHECK_FALSE(coordinator.SaveAsPending());
    CHECK_FALSE(coordinator.Due(69'999U));
    CHECK(coordinator.Due(70'000U));
  }
}

TEST_CASE("coordinator cadence handles a wrapping millisecond clock") {
  constexpr std::uint32_t start =
      std::numeric_limits<std::uint32_t>::max() - 1'000U;
  AutoSaveCoordinator coordinator;
  coordinator.OnProjectLoaded(start);
  coordinator.MarkDirty(start + 1U);

  CHECK(coordinator.Tick(start + 59'999U, Eligible()) ==
        AutoSaveCoordinator::TickResult::Idle);
  CHECK(coordinator.Tick(start + 60'000U, Eligible()) ==
        AutoSaveCoordinator::TickResult::SaveRequested);
}
