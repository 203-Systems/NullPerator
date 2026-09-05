/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Instruments/SampleEditorFileJournal.h"
#include "Services/Audio/WavHeader.h"
#include "System/FileSystem/FileSystem.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ui2 {

enum class Ui2SampleEditorTransactionResult : std::uint8_t {
  Ready,
  InProgress,
  NoChanges,
  Applied,
  Saved,
  Discarded,
  Cancelled,
  InvalidPath,
  InvalidSource,
  RecoveryFailed,
  CopyFailed,
  MutationFailed,
  // The destination is authoritative and a validated working generation is
  // still present. This is the only save failure that may retry in place.
  SaveFailedRetryable,
  // No retryable working generation is guaranteed. The application must
  // reopen/reload the authoritative destination before accepting more input.
  SaveFailed,
  DiscardFailed,
};

// One editor-wide, fixed-capacity journal. SAVE/DISCARD remain atomic
// transaction boundaries. Potentially multi-megabyte Apply work is advanced
// cooperatively from the application owner task with a strict per-step payload
// I/O budget, so Node/WASM and ESP32 share one non-blocking implementation.
class Ui2SampleEditorTransaction final {
public:
  static constexpr std::size_t ScratchBytes = 2048U;
  // Covers a read/write step for the largest supported two-channel PCM/float
  // frame. Smaller budgets are rejected rather than spinning without I/O.
  static constexpr std::uint32_t MinimumStepBudget = 32U;
  static constexpr std::uint32_t DefaultStepBudget = 16U * 1024U;

  [[nodiscard]] Ui2SampleEditorTransactionResult Begin(FileSystem &fileSystem,
                                                       const char *destination);

  [[nodiscard]] Ui2SampleEditorTransactionResult BeginTrim(std::uint32_t start,
                                                           std::uint32_t end);
  [[nodiscard]] Ui2SampleEditorTransactionResult BeginNormalize();
  [[nodiscard]] Ui2SampleEditorTransactionResult
  StepApply(std::uint32_t payloadIoBudget = DefaultStepBudget);
  [[nodiscard]] Ui2SampleEditorTransactionResult CancelApply();

  // Synchronous compatibility wrappers are retained for non-UI callers and
  // focused transaction tests. The product Sample Editor uses Begin*/StepApply
  // exclusively; grep-enforced tests protect that owner-task boundary.
  [[nodiscard]] Ui2SampleEditorTransactionResult ApplyTrim(std::uint32_t start,
                                                           std::uint32_t end);
  [[nodiscard]] Ui2SampleEditorTransactionResult ApplyNormalize();

  [[nodiscard]] Ui2SampleEditorTransactionResult Save();
  [[nodiscard]] Ui2SampleEditorTransactionResult Discard();
  void Reset();

  [[nodiscard]] bool Active() const { return active_; }
  [[nodiscard]] bool ApplyActive() const { return phase_ != ApplyPhase::Idle; }
  [[nodiscard]] bool HasWorkingCopy() const { return hasWorkingCopy_; }
  [[nodiscard]] std::uint8_t ApplyProgress() const { return progress_; }
  [[nodiscard]] std::uint32_t LastStepPayloadIoBytes() const {
    return lastStepPayloadIoBytes_;
  }
  [[nodiscard]] const char *DestinationPath() const {
    return destination_.data();
  }
  [[nodiscard]] const char *WorkingPath() const;
  [[nodiscard]] const char *BackupPath() const { return backup_.data(); }

private:
  enum class ApplyKind : std::uint8_t { None, Trim, Normalize };
  enum class ApplyPhase : std::uint8_t {
    Idle,
    NormalizeScan,
    CopyWorking,
    TrimMove,
    NormalizeWrite,
  };

  [[nodiscard]] bool CopyPath(const char *source,
                              std::array<char, PFILENAME_SIZE> &destination);
  [[nodiscard]] bool BuildPaths(const char *destination);
  [[nodiscard]] bool Validate(const char *path) const;
  [[nodiscard]] bool Recover();
  [[nodiscard]] bool RestoreBackup();
  [[nodiscard]] Ui2SampleEditorTransactionResult ClassifyPromotionFailure();
  [[nodiscard]] bool ReadApplyHeader(const char *path, FileHandle &file);
  [[nodiscard]] Ui2SampleEditorTransactionResult
  BeginCopy(const char *sourcePath);
  [[nodiscard]] Ui2SampleEditorTransactionResult BeginTrimMutation();
  [[nodiscard]] Ui2SampleEditorTransactionResult BeginNormalizeWrite();
  [[nodiscard]] Ui2SampleEditorTransactionResult CompleteCopy();
  [[nodiscard]] Ui2SampleEditorTransactionResult CompleteNormalizeScan();
  [[nodiscard]] Ui2SampleEditorTransactionResult CompleteTrim();
  [[nodiscard]] Ui2SampleEditorTransactionResult CompleteNormalizeWrite();
  [[nodiscard]] Ui2SampleEditorTransactionResult CommitStaging();
  [[nodiscard]] Ui2SampleEditorTransactionResult
  FinishApply(Ui2SampleEditorTransactionResult result);
  [[nodiscard]] Ui2SampleEditorTransactionResult
  FailApply(Ui2SampleEditorTransactionResult result);
  void CloseApplyHandles();
  void ResetApplyState();
  void UpdateProgress();
  [[nodiscard]] const char *StagingPath() const;

  FileSystem *fileSystem_ = nullptr;
  std::array<char, PFILENAME_SIZE> destination_{};
  std::array<char, PFILENAME_SIZE> working_{};
  std::array<char, PFILENAME_SIZE> operation_{};
  std::array<char, PFILENAME_SIZE> backup_{};
  std::array<std::uint8_t, ScratchBytes> scratch_{};
  FileHandle readFile_{};
  FileHandle writeFile_{};
  WavHeaderInfo applyHeader_{};
  std::uint64_t totalWork_ = 0U;
  std::uint64_t completedWork_ = 0U;
  std::uint32_t sourceSize_ = 0U;
  std::uint32_t bytesRemaining_ = 0U;
  std::uint32_t readOffset_ = 0U;
  std::uint32_t writeOffset_ = 0U;
  std::uint32_t trimStartFrame_ = 0U;
  std::uint32_t trimFrames_ = 0U;
  std::uint32_t normalizePeak_ = 0U;
  std::uint32_t normalizeGainQ16_ = 0U;
  std::uint32_t lastStepPayloadIoBytes_ = 0U;
  ApplyKind applyKind_ = ApplyKind::None;
  ApplyPhase phase_ = ApplyPhase::Idle;
  std::uint8_t progress_ = 0U;
  bool initialHadWorkingCopy_ = false;
  bool stagingUsesOperation_ = false;
  bool stagingTouched_ = false;
  bool workingUsesOperation_ = false;
  bool hasWorkingCopy_ = false;
  bool active_ = false;
};

static_assert(!std::is_polymorphic_v<Ui2SampleEditorTransaction>);
static_assert(sizeof(Ui2SampleEditorTransaction) <= 3'800U);

} // namespace ui2
