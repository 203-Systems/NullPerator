/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Instruments/SampleEditorFileJournal.h"
#include "Application/Instruments/WavFileWriter.h"
#include "System/FileSystem/FileSystem.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace ui2 {

enum class Ui2SampleEditorTransactionResult : std::uint8_t {
  Ready,
  NoChanges,
  Applied,
  Saved,
  Discarded,
  InvalidPath,
  InvalidSource,
  RecoveryFailed,
  CopyFailed,
  MutationFailed,
  SaveFailed,
  DiscardFailed,
};

// One editor-wide, fixed-capacity journal. Edits are made only against the
// hidden sibling working copy. SAVE promotes that copy while retaining the
// original as a backup until the replacement has been structurally validated.
// This keeps a failed trim/normalize or an interrupted FAT rename from
// corrupting the user's authoritative sample.
class Ui2SampleEditorTransaction final {
public:
  static constexpr std::size_t ScratchBytes = 2048U;

  [[nodiscard]] Ui2SampleEditorTransactionResult
  Begin(FileSystem &fileSystem, const char *destination) {
    Reset();
    if (!CopyPath(destination, destination_) ||
        !BuildSiblingPath(destination,
                          SampleEditorFileJournal::Generation::Working,
                          working_) ||
        !BuildSiblingPath(destination,
                          SampleEditorFileJournal::Generation::Backup,
                          backup_) ||
        std::strcmp(destination_.data(), working_.data()) == 0 ||
        std::strcmp(destination_.data(), backup_.data()) == 0 ||
        std::strcmp(working_.data(), backup_.data()) == 0) {
      Reset();
      return Ui2SampleEditorTransactionResult::InvalidPath;
    }

    fileSystem_ = &fileSystem;
    if (!Recover()) {
      Reset();
      return Ui2SampleEditorTransactionResult::RecoveryFailed;
    }
    if (!Validate(destination_.data())) {
      Reset();
      return Ui2SampleEditorTransactionResult::InvalidSource;
    }
    if (fileSystem_->exists(working_.data()) &&
        !fileSystem_->DeleteFile(working_.data())) {
      Reset();
      return Ui2SampleEditorTransactionResult::RecoveryFailed;
    }
    active_ = true;
    return Ui2SampleEditorTransactionResult::Ready;
  }

  [[nodiscard]] Ui2SampleEditorTransactionResult
  ApplyTrim(std::uint32_t start, std::uint32_t end) {
    const bool hadWorkingCopy = hasWorkingCopy_;
    if (!EnsureWorkingCopy())
      return Ui2SampleEditorTransactionResult::CopyFailed;
    if (FileSystem::GetInstance() != fileSystem_) {
      AbandonBrokenWorkingCopy();
      return Ui2SampleEditorTransactionResult::MutationFailed;
    }
    WavTrimResult result{};
    const bool applied = WavFileWriter::TrimFile(
        working_.data(), start, end, scratch_.data(), scratch_.size(), result);
    return FinishMutation(applied, result.trimmed, hadWorkingCopy);
  }

  [[nodiscard]] Ui2SampleEditorTransactionResult ApplyNormalize() {
    const bool hadWorkingCopy = hasWorkingCopy_;
    if (!EnsureWorkingCopy())
      return Ui2SampleEditorTransactionResult::CopyFailed;
    if (FileSystem::GetInstance() != fileSystem_) {
      AbandonBrokenWorkingCopy();
      return Ui2SampleEditorTransactionResult::MutationFailed;
    }
    WavNormalizeResult result{};
    const bool applied = WavFileWriter::NormalizeFile(
        working_.data(), scratch_.data(), scratch_.size(), result);
    return FinishMutation(applied, result.normalized, hadWorkingCopy);
  }

  [[nodiscard]] Ui2SampleEditorTransactionResult Save() {
    if (!active_ || fileSystem_ == nullptr)
      return Ui2SampleEditorTransactionResult::SaveFailed;
    // A backup may be the only authoritative generation after an interrupted
    // promotion. Recover it before retrying; never delete it merely because a
    // prior SAVE attempt left it behind.
    if (fileSystem_->exists(backup_.data()) && !Recover())
      return Ui2SampleEditorTransactionResult::RecoveryFailed;
    if (!hasWorkingCopy_)
      return Ui2SampleEditorTransactionResult::NoChanges;
    if (!Validate(working_.data())) {
      AbandonBrokenWorkingCopy();
      return Ui2SampleEditorTransactionResult::SaveFailed;
    }
    if (!fileSystem_->MoveFile(destination_.data(), backup_.data()))
      return Ui2SampleEditorTransactionResult::SaveFailed;
    if (!fileSystem_->MoveFile(working_.data(), destination_.data())) {
      return RestoreBackup() ? Ui2SampleEditorTransactionResult::SaveFailed
                             : Ui2SampleEditorTransactionResult::RecoveryFailed;
    }
    hasWorkingCopy_ = false;
    if (!Validate(destination_.data())) {
      return RestoreBackup() ? Ui2SampleEditorTransactionResult::SaveFailed
                             : Ui2SampleEditorTransactionResult::RecoveryFailed;
    }

    // The new destination is already the committed generation. A stale
    // backup is safe and is removed by Begin() after a crash or cleanup error.
    (void)fileSystem_->DeleteFile(backup_.data());
    active_ = false;
    return Ui2SampleEditorTransactionResult::Saved;
  }

  [[nodiscard]] Ui2SampleEditorTransactionResult Discard() {
    if (fileSystem_ != nullptr && fileSystem_->exists(working_.data()) &&
        !fileSystem_->DeleteFile(working_.data()))
      return Ui2SampleEditorTransactionResult::DiscardFailed;
    hasWorkingCopy_ = false;
    active_ = false;
    return Ui2SampleEditorTransactionResult::Discarded;
  }

  void Reset() {
    fileSystem_ = nullptr;
    destination_.fill('\0');
    working_.fill('\0');
    backup_.fill('\0');
    hasWorkingCopy_ = false;
    active_ = false;
  }

  [[nodiscard]] bool Active() const { return active_; }
  [[nodiscard]] bool HasWorkingCopy() const { return hasWorkingCopy_; }
  [[nodiscard]] const char *DestinationPath() const {
    return destination_.data();
  }
  [[nodiscard]] const char *WorkingPath() const { return working_.data(); }
  [[nodiscard]] const char *BackupPath() const { return backup_.data(); }

private:
  template <std::size_t Capacity>
  static bool CopyPath(const char *source,
                       std::array<char, Capacity> &destination) {
    destination.fill('\0');
    if (source == nullptr || source[0] == '\0')
      return false;
    const int written =
        std::snprintf(destination.data(), destination.size(), "%s", source);
    return written > 0 &&
           static_cast<std::size_t>(written) < destination.size();
  }

  template <std::size_t Capacity>
  static bool BuildSiblingPath(const char *source,
                               SampleEditorFileJournal::Generation generation,
                               std::array<char, Capacity> &destination) {
    destination.fill('\0');
    return SampleEditorFileJournal::BuildPath(
        source, generation, destination.data(), destination.size());
  }

  [[nodiscard]] bool Validate(const char *path) const {
    return fileSystem_ != nullptr &&
           SampleEditorFileJournal::ValidateWav(*fileSystem_, path);
  }

  [[nodiscard]] bool Recover() {
    if (fileSystem_ == nullptr)
      return false;
    return SampleEditorFileJournal::RecoverDestination(*fileSystem_,
                                                       destination_.data());
  }

  [[nodiscard]] bool EnsureWorkingCopy() {
    if (!active_ || fileSystem_ == nullptr)
      return false;
    if (hasWorkingCopy_)
      return Validate(working_.data());
    if (fileSystem_->exists(working_.data()) &&
        !fileSystem_->DeleteFile(working_.data()))
      return false;
    if (!CopyToWorking(destination_.data()) ||
        !Validate(working_.data())) {
      (void)fileSystem_->DeleteFile(working_.data());
      return false;
    }
    hasWorkingCopy_ = true;
    return true;
  }

  [[nodiscard]] bool CopyToWorking(const char *sourcePath) {
    FileHandle source = fileSystem_->Open(sourcePath, "r");
    FileHandle destination = fileSystem_->Open(working_.data(), "w");
    if (!source || !destination)
      return false;
    while (true) {
      const int bytesRead =
          source->Read(scratch_.data(), static_cast<int>(scratch_.size()));
      if (bytesRead < 0 || (bytesRead == 0 && source->Error() != 0))
        return false;
      if (bytesRead == 0)
        break;
      if (destination->Write(scratch_.data(), 1, bytesRead) != bytesRead)
        return false;
    }
    return destination->Sync();
  }

  [[nodiscard]] Ui2SampleEditorTransactionResult
  FinishMutation(bool applied, bool changed, bool hadWorkingCopy) {
    if (!applied || !Validate(working_.data())) {
      AbandonBrokenWorkingCopy();
      return Ui2SampleEditorTransactionResult::MutationFailed;
    }
    if (!changed) {
      // A scan-only no-op must not leave behind the copy it just created. If
      // an earlier operation already produced edits, retain that generation.
      if (!hadWorkingCopy) {
        if (!fileSystem_->DeleteFile(working_.data())) {
          AbandonBrokenWorkingCopy();
          return Ui2SampleEditorTransactionResult::MutationFailed;
        }
        hasWorkingCopy_ = false;
      }
      return Ui2SampleEditorTransactionResult::NoChanges;
    }
    return Ui2SampleEditorTransactionResult::Applied;
  }

  void AbandonBrokenWorkingCopy() {
    if (fileSystem_ != nullptr && fileSystem_->exists(working_.data()))
      (void)fileSystem_->DeleteFile(working_.data());
    hasWorkingCopy_ = false;
  }

  [[nodiscard]] bool RestoreBackup() {
    if (fileSystem_ == nullptr || !fileSystem_->exists(backup_.data()))
      return false;
    if (fileSystem_->exists(destination_.data()) &&
        !fileSystem_->DeleteFile(destination_.data()))
      return false;
    return fileSystem_->MoveFile(backup_.data(), destination_.data()) &&
           Validate(destination_.data());
  }

  FileSystem *fileSystem_ = nullptr;
  std::array<char, PFILENAME_SIZE> destination_{};
  std::array<char, PFILENAME_SIZE> working_{};
  std::array<char, PFILENAME_SIZE> backup_{};
  std::array<std::uint8_t, ScratchBytes> scratch_{};
  bool hasWorkingCopy_ = false;
  bool active_ = false;
};

static_assert(!std::is_polymorphic_v<Ui2SampleEditorTransaction>);
static_assert(sizeof(Ui2SampleEditorTransaction) <= 3'000U);

} // namespace ui2
