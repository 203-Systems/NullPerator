/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>
#include <type_traits>

// Platform-neutral autosave cadence and eligibility policy. The coordinator
// owns only fixed-size lifecycle state; the application remains responsible
// for calling TrackerApplicationSession::AutoSave() when Tick() requests it.
class AutoSaveCoordinator final {
public:
  static constexpr std::uint32_t IntervalMs = 60'000U;

  struct Conditions final {
    bool projectLoaded = false;
    bool playerRunning = false;
    bool recordingActive = false;
    bool operationAllowsSave = false;
  };

  enum class TickResult : std::uint8_t {
    Idle,
    Deferred,
    SaveRequested,
  };

  constexpr AutoSaveCoordinator() noexcept = default;
  explicit constexpr AutoSaveCoordinator(std::uint32_t nowMs) noexcept
      : cadenceStartedMs_(nowMs), cadenceArmed_(true) {}

  void OnProjectLoaded(std::uint32_t nowMs) noexcept;
  void OnProjectCreated(std::uint32_t nowMs) noexcept;
  void OnProjectSaved(std::uint32_t nowMs) noexcept;
  void OnProjectClosed(std::uint32_t nowMs) noexcept;

  // MarkDirty intentionally does not postpone an armed deadline: autosave is
  // a cadence, not an input debounce. The timestamp only establishes a first
  // deadline when no project lifecycle event has armed one yet.
  void MarkDirty(std::uint32_t nowMs) noexcept;
  void SetPersistBusy(bool busy) noexcept;
  void SetSaveAsPending(bool pending) noexcept;

  [[nodiscard]] TickResult Tick(std::uint32_t nowMs,
                                const Conditions &conditions) noexcept;
  void CompleteAutoSave(std::uint32_t nowMs, bool succeeded) noexcept;

  [[nodiscard]] bool Dirty() const noexcept {
    return revision_ != persistedRevision_;
  }
  [[nodiscard]] bool PersistBusy() const noexcept { return persistBusy_; }
  [[nodiscard]] bool SaveAsPending() const noexcept { return saveAsPending_; }
  [[nodiscard]] bool AutoSaveInFlight() const noexcept {
    return autoSaveInFlight_;
  }
  [[nodiscard]] bool Due(std::uint32_t nowMs) const noexcept;

private:
  void ResetProjectState(std::uint32_t nowMs) noexcept;
  void AdvanceCadence(std::uint32_t nowMs) noexcept;

  std::uint32_t cadenceStartedMs_ = 0U;
  std::uint32_t revision_ = 0U;
  std::uint32_t persistedRevision_ = 0U;
  std::uint32_t savingRevision_ = 0U;
  bool cadenceArmed_ = false;
  bool persistBusy_ = false;
  bool saveAsPending_ = false;
  bool autoSaveInFlight_ = false;
};

static_assert(std::is_trivially_copyable_v<AutoSaveCoordinator>);
static_assert(sizeof(AutoSaveCoordinator) <= 24U,
              "autosave lifecycle state must stay embedded-friendly");
