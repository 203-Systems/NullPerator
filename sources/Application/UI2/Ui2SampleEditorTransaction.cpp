/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Ui2SampleEditorTransaction.h"

#include "Application/Instruments/WavHeader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace ui2 {

bool Ui2SampleEditorTransaction::CopyPath(
    const char *source, std::array<char, PFILENAME_SIZE> &destination) {
  destination.fill('\0');
  if (source == nullptr || source[0] == '\0')
    return false;
  const int written =
      std::snprintf(destination.data(), destination.size(), "%s", source);
  return written > 0 &&
         static_cast<std::size_t>(written) < destination.size();
}

bool Ui2SampleEditorTransaction::BuildPaths(const char *destination) {
  return CopyPath(destination, destination_) &&
         SampleEditorFileJournal::BuildPath(
             destination, SampleEditorFileJournal::Generation::Working,
             working_.data(), working_.size()) &&
         SampleEditorFileJournal::BuildPath(
             destination, SampleEditorFileJournal::Generation::Operation,
             operation_.data(), operation_.size()) &&
         SampleEditorFileJournal::BuildPath(
             destination, SampleEditorFileJournal::Generation::Backup,
             backup_.data(), backup_.size()) &&
         std::strcmp(destination_.data(), working_.data()) != 0 &&
         std::strcmp(destination_.data(), operation_.data()) != 0 &&
         std::strcmp(destination_.data(), backup_.data()) != 0 &&
         std::strcmp(working_.data(), operation_.data()) != 0 &&
         std::strcmp(working_.data(), backup_.data()) != 0 &&
         std::strcmp(operation_.data(), backup_.data()) != 0;
}

const char *Ui2SampleEditorTransaction::WorkingPath() const {
  return workingUsesOperation_ ? operation_.data() : working_.data();
}

const char *Ui2SampleEditorTransaction::StagingPath() const {
  return stagingUsesOperation_ ? operation_.data() : working_.data();
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::Begin(FileSystem &fileSystem,
                                  const char *destination) {
  Reset();
  if (!BuildPaths(destination)) {
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
  if (fileSystem_->exists(operation_.data()) &&
      !fileSystem_->DeleteFile(operation_.data())) {
    Reset();
    return Ui2SampleEditorTransactionResult::RecoveryFailed;
  }
  active_ = true;
  return Ui2SampleEditorTransactionResult::Ready;
}

bool Ui2SampleEditorTransaction::Validate(const char *path) const {
  return fileSystem_ != nullptr &&
         SampleEditorFileJournal::ValidateWav(*fileSystem_, path);
}

bool Ui2SampleEditorTransaction::Recover() {
  return fileSystem_ != nullptr &&
         SampleEditorFileJournal::RecoverDestination(*fileSystem_,
                                                     destination_.data());
}

bool Ui2SampleEditorTransaction::RestoreBackup() {
  if (fileSystem_ == nullptr || !fileSystem_->exists(backup_.data()))
    return false;
  if (fileSystem_->exists(destination_.data()) &&
      !fileSystem_->DeleteFile(destination_.data()))
    return false;
  return fileSystem_->MoveFile(backup_.data(), destination_.data()) &&
         Validate(destination_.data());
}

Ui2SampleEditorTransactionResult Ui2SampleEditorTransaction::Save() {
  if (!active_ || fileSystem_ == nullptr || ApplyActive())
    return Ui2SampleEditorTransactionResult::SaveFailed;
  if (fileSystem_->exists(backup_.data()) && !Recover())
    return Ui2SampleEditorTransactionResult::RecoveryFailed;
  if (!hasWorkingCopy_)
    return Ui2SampleEditorTransactionResult::NoChanges;
  const char *const edited = WorkingPath();
  if (!Validate(edited)) {
    (void)fileSystem_->DeleteFile(edited);
    hasWorkingCopy_ = false;
    return Ui2SampleEditorTransactionResult::SaveFailed;
  }
  if (!fileSystem_->MoveFile(destination_.data(), backup_.data()))
    return Ui2SampleEditorTransactionResult::SaveFailed;
  if (!fileSystem_->MoveFile(edited, destination_.data())) {
    return RestoreBackup() ? Ui2SampleEditorTransactionResult::SaveFailed
                           : Ui2SampleEditorTransactionResult::RecoveryFailed;
  }
  hasWorkingCopy_ = false;
  if (!Validate(destination_.data())) {
    return RestoreBackup() ? Ui2SampleEditorTransactionResult::SaveFailed
                           : Ui2SampleEditorTransactionResult::RecoveryFailed;
  }
  (void)fileSystem_->DeleteFile(backup_.data());
  const char *const inactive = workingUsesOperation_ ? working_.data()
                                                     : operation_.data();
  if (fileSystem_->exists(inactive))
    (void)fileSystem_->DeleteFile(inactive);
  active_ = false;
  return Ui2SampleEditorTransactionResult::Saved;
}

Ui2SampleEditorTransactionResult Ui2SampleEditorTransaction::Discard() {
  if (ApplyActive()) {
    const Ui2SampleEditorTransactionResult cancelled = CancelApply();
    if (cancelled != Ui2SampleEditorTransactionResult::Cancelled)
      return Ui2SampleEditorTransactionResult::DiscardFailed;
  }
  if (fileSystem_ != nullptr) {
    const char *const inactive = workingUsesOperation_ ? working_.data()
                                                       : operation_.data();
    if (fileSystem_->exists(inactive) && !fileSystem_->DeleteFile(inactive))
      return Ui2SampleEditorTransactionResult::DiscardFailed;
    const char *const edited = WorkingPath();
    if (fileSystem_->exists(edited) && !fileSystem_->DeleteFile(edited))
      return Ui2SampleEditorTransactionResult::DiscardFailed;
  }
  hasWorkingCopy_ = false;
  active_ = false;
  return Ui2SampleEditorTransactionResult::Discarded;
}

void Ui2SampleEditorTransaction::CloseApplyHandles() {
  readFile_.reset();
  writeFile_.reset();
}

void Ui2SampleEditorTransaction::ResetApplyState() {
  CloseApplyHandles();
  applyHeader_ = {};
  totalWork_ = completedWork_ = 0U;
  sourceSize_ = bytesRemaining_ = readOffset_ = writeOffset_ = 0U;
  trimStartFrame_ = trimFrames_ = normalizePeak_ = normalizeGainQ16_ = 0U;
  lastStepPayloadIoBytes_ = 0U;
  applyKind_ = ApplyKind::None;
  phase_ = ApplyPhase::Idle;
  progress_ = 0U;
  initialHadWorkingCopy_ = false;
  stagingUsesOperation_ = false;
  stagingTouched_ = false;
}

void Ui2SampleEditorTransaction::Reset() {
  ResetApplyState();
  fileSystem_ = nullptr;
  destination_.fill('\0');
  working_.fill('\0');
  operation_.fill('\0');
  backup_.fill('\0');
  workingUsesOperation_ = false;
  hasWorkingCopy_ = false;
  active_ = false;
}

bool Ui2SampleEditorTransaction::ReadApplyHeader(const char *path,
                                                 FileHandle &file) {
  if (!active_ || fileSystem_ == nullptr || path == nullptr)
    return false;
  file = fileSystem_->Open(path, "r");
  if (!file)
    return false;
  auto header = WavHeaderWriter::ReadHeader(file.get());
  if (!header.has_value() || header->numChannels == 0U ||
      header->numChannels > 2U || header->blockAlign == 0U ||
      header->dataChunkSize / header->blockAlign == 0U)
    return false;
  applyHeader_ = *header;
  file->Seek(0, SEEK_END);
  const long size = file->Tell();
  if (size <= 0 || file->Error() != 0 ||
      static_cast<unsigned long>(size) >
          std::numeric_limits<std::uint32_t>::max())
    return false;
  sourceSize_ = static_cast<std::uint32_t>(size);
  return true;
}

void Ui2SampleEditorTransaction::UpdateProgress() {
  if (totalWork_ == 0U) {
    progress_ = 0U;
    return;
  }
  const std::uint64_t bounded = std::min(completedWork_, totalWork_);
  progress_ = static_cast<std::uint8_t>(
      std::min<std::uint64_t>(99U, bounded * 100U / totalWork_));
}

Ui2SampleEditorTransactionResult Ui2SampleEditorTransaction::FinishApply(
    Ui2SampleEditorTransactionResult result) {
  CloseApplyHandles();
  phase_ = ApplyPhase::Idle;
  applyKind_ = ApplyKind::None;
  completedWork_ = totalWork_;
  progress_ = 100U;
  initialHadWorkingCopy_ = false;
  stagingTouched_ = false;
  return result;
}

Ui2SampleEditorTransactionResult Ui2SampleEditorTransaction::FailApply(
    Ui2SampleEditorTransactionResult result) {
  CloseApplyHandles();
  bool cleaned = true;
  if (stagingTouched_ && fileSystem_ != nullptr &&
      fileSystem_->exists(StagingPath()))
    cleaned = fileSystem_->DeleteFile(StagingPath());
  hasWorkingCopy_ = initialHadWorkingCopy_;
  ResetApplyState();
  return cleaned ? result : Ui2SampleEditorTransactionResult::RecoveryFailed;
}

Ui2SampleEditorTransactionResult Ui2SampleEditorTransaction::CancelApply() {
  if (!ApplyActive())
    return Ui2SampleEditorTransactionResult::Cancelled;
  CloseApplyHandles();
  bool cleaned = true;
  if (stagingTouched_ && fileSystem_ != nullptr &&
      fileSystem_->exists(StagingPath()))
    cleaned = fileSystem_->DeleteFile(StagingPath());
  hasWorkingCopy_ = initialHadWorkingCopy_;
  ResetApplyState();
  return cleaned ? Ui2SampleEditorTransactionResult::Cancelled
                 : Ui2SampleEditorTransactionResult::RecoveryFailed;
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::BeginTrim(std::uint32_t start,
                                      std::uint32_t end) {
  if (!active_ || fileSystem_ == nullptr || ApplyActive() || end < start)
    return Ui2SampleEditorTransactionResult::MutationFailed;
  ResetApplyState();
  initialHadWorkingCopy_ = hasWorkingCopy_;
  stagingUsesOperation_ = hasWorkingCopy_ && !workingUsesOperation_;
  applyKind_ = ApplyKind::Trim;
  const char *sourcePath = hasWorkingCopy_ ? WorkingPath()
                                           : destination_.data();
  if (!ReadApplyHeader(sourcePath, readFile_))
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);

  const std::uint32_t totalFrames =
      applyHeader_.dataChunkSize / applyHeader_.blockAlign;
  const std::uint32_t clampedStart = std::min(start, totalFrames - 1U);
  const std::uint32_t clampedEnd = std::min(end, totalFrames - 1U);
  if (clampedStart > clampedEnd)
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
  trimFrames_ = clampedEnd - clampedStart + 1U;
  trimStartFrame_ = clampedStart;
  if (clampedStart == 0U && trimFrames_ == totalFrames) {
    return FinishApply(Ui2SampleEditorTransactionResult::NoChanges);
  }

  bytesRemaining_ = trimFrames_ * applyHeader_.blockAlign;
  totalWork_ = bytesRemaining_ + sourceSize_;
  readFile_.reset();
  return BeginCopy(sourcePath);
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::BeginNormalize() {
  if (!active_ || fileSystem_ == nullptr || ApplyActive())
    return Ui2SampleEditorTransactionResult::MutationFailed;
  ResetApplyState();
  initialHadWorkingCopy_ = hasWorkingCopy_;
  stagingUsesOperation_ = hasWorkingCopy_ && !workingUsesOperation_;
  applyKind_ = ApplyKind::Normalize;
  const char *sourcePath = hasWorkingCopy_ ? WorkingPath()
                                           : destination_.data();
  if (!ReadApplyHeader(sourcePath, readFile_))
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
  const bool supportedPcm =
      applyHeader_.audioFormat == 1U &&
      (applyHeader_.bitsPerSample == 8U ||
       applyHeader_.bitsPerSample == 16U);
  if (!supportedPcm)
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);

  totalWork_ = static_cast<std::uint64_t>(applyHeader_.dataChunkSize) * 2U +
               sourceSize_;
  bytesRemaining_ = applyHeader_.dataChunkSize;
  readOffset_ = applyHeader_.dataOffset;
  normalizePeak_ = 0U;
  readFile_->Seek(static_cast<long>(readOffset_), SEEK_SET);
  if (readFile_->Error() != 0)
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
  phase_ = ApplyPhase::NormalizeScan;
  return Ui2SampleEditorTransactionResult::InProgress;
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::BeginCopy(const char *sourcePath) {
  if (fileSystem_->exists(StagingPath()) &&
      !fileSystem_->DeleteFile(StagingPath()))
    return FailApply(Ui2SampleEditorTransactionResult::RecoveryFailed);
  readFile_ = fileSystem_->Open(sourcePath, "r");
  stagingTouched_ = true;
  writeFile_ = fileSystem_->Open(StagingPath(), "w");
  if (!readFile_ || !writeFile_)
    return FailApply(Ui2SampleEditorTransactionResult::CopyFailed);
  readFile_->Seek(0, SEEK_SET);
  bytesRemaining_ = sourceSize_;
  readOffset_ = 0U;
  writeOffset_ = 0U;
  phase_ = ApplyPhase::CopyWorking;
  return Ui2SampleEditorTransactionResult::InProgress;
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::BeginTrimMutation() {
  CloseApplyHandles();
  readFile_ = fileSystem_->Open(StagingPath(), "r+");
  if (!readFile_)
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
  readOffset_ = applyHeader_.dataOffset +
                trimStartFrame_ * applyHeader_.blockAlign;
  writeOffset_ = applyHeader_.dataOffset;
  bytesRemaining_ = trimFrames_ * applyHeader_.blockAlign;
  phase_ = ApplyPhase::TrimMove;
  return Ui2SampleEditorTransactionResult::InProgress;
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::BeginNormalizeWrite() {
  CloseApplyHandles();
  readFile_ = fileSystem_->Open(StagingPath(), "r+");
  if (!readFile_)
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
  bytesRemaining_ = applyHeader_.dataChunkSize;
  readOffset_ = applyHeader_.dataOffset;
  readFile_->Seek(static_cast<long>(readOffset_), SEEK_SET);
  if (readFile_->Error() != 0)
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
  phase_ = ApplyPhase::NormalizeWrite;
  return Ui2SampleEditorTransactionResult::InProgress;
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::CompleteCopy() {
  if (!readFile_ || !writeFile_ || readFile_->Error() != 0 ||
      !writeFile_->Sync())
    return FailApply(Ui2SampleEditorTransactionResult::CopyFailed);
  CloseApplyHandles();
  if (!Validate(StagingPath()))
    return FailApply(Ui2SampleEditorTransactionResult::CopyFailed);
  return applyKind_ == ApplyKind::Trim ? BeginTrimMutation()
                                       : BeginNormalizeWrite();
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::CompleteNormalizeScan() {
  readFile_.reset();
  const std::uint32_t fullScale =
      applyHeader_.bitsPerSample == 8U ? 127U : 32767U;
  if (normalizePeak_ == 0U || normalizePeak_ >= fullScale)
    return FinishApply(Ui2SampleEditorTransactionResult::NoChanges);
  normalizeGainQ16_ = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(fullScale) << 16U) / normalizePeak_);
  return BeginCopy(initialHadWorkingCopy_ ? WorkingPath()
                                         : destination_.data());
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::CommitStaging() {
  if (!Validate(StagingPath()))
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
  if (initialHadWorkingCopy_) {
    const char *const previous = WorkingPath();
    if (fileSystem_->exists(previous) && !fileSystem_->DeleteFile(previous))
      return FailApply(Ui2SampleEditorTransactionResult::RecoveryFailed);
  }
  workingUsesOperation_ = stagingUsesOperation_;
  hasWorkingCopy_ = true;
  stagingTouched_ = false;
  return FinishApply(Ui2SampleEditorTransactionResult::Applied);
}

Ui2SampleEditorTransactionResult Ui2SampleEditorTransaction::CompleteTrim() {
  if (!readFile_)
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
  const std::uint32_t newDataSize = trimFrames_ * applyHeader_.blockAlign;
  readFile_->Seek(static_cast<long>(applyHeader_.dataOffset + newDataSize),
                  SEEK_SET);
  if (!WavHeaderWriter::UpdateFileSize(
          readFile_.get(), trimFrames_, applyHeader_.numChannels,
          applyHeader_.bytesPerSample))
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
  readFile_.reset();
  return CommitStaging();
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::CompleteNormalizeWrite() {
  if (!readFile_ || !readFile_->Sync())
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
  readFile_.reset();
  return CommitStaging();
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::StepApply(std::uint32_t payloadIoBudget) {
  if (!ApplyActive())
    return Ui2SampleEditorTransactionResult::MutationFailed;
  if (payloadIoBudget < MinimumStepBudget)
    return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
  lastStepPayloadIoBytes_ = 0U;
  while (ApplyActive() && lastStepPayloadIoBytes_ < payloadIoBudget) {
    const std::uint32_t available =
        payloadIoBudget - lastStepPayloadIoBytes_;
    const bool readWrite = phase_ != ApplyPhase::NormalizeScan;
    const std::uint32_t ioFactor = readWrite ? 2U : 1U;
    std::uint32_t chunk = std::min<std::uint32_t>(
        bytesRemaining_, std::min<std::uint32_t>(
                             static_cast<std::uint32_t>(scratch_.size()),
                             available / ioFactor));
    if (phase_ == ApplyPhase::NormalizeScan ||
        phase_ == ApplyPhase::NormalizeWrite) {
      chunk -= chunk % applyHeader_.blockAlign;
    }
    if (chunk == 0U) {
      // An odd remaining budget may be smaller than one aligned frame after
      // this step already made progress. Yield now; only a budget that cannot
      // advance at all is invalid.
      if (lastStepPayloadIoBytes_ != 0U)
        return Ui2SampleEditorTransactionResult::InProgress;
      return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
    }

    if (phase_ == ApplyPhase::NormalizeScan) {
      const int bytesRead =
          readFile_->Read(scratch_.data(), static_cast<int>(chunk));
      if (bytesRead != static_cast<int>(chunk))
        return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
      if (applyHeader_.bitsPerSample == 8U) {
        for (std::uint32_t index = 0U; index < chunk; ++index) {
          const std::int32_t centered =
              static_cast<std::int32_t>(scratch_[index]) - 128;
          const std::uint32_t magnitude = static_cast<std::uint32_t>(
              centered < 0 ? -centered : centered);
          normalizePeak_ = std::max(normalizePeak_, magnitude);
        }
      } else {
        for (std::uint32_t index = 0U; index < chunk; index += 2U) {
          const std::uint16_t encoded =
              static_cast<std::uint16_t>(scratch_[index]) |
              static_cast<std::uint16_t>(scratch_[index + 1U] << 8U);
          const std::int32_t sample =
              static_cast<std::int16_t>(encoded);
          const std::uint32_t magnitude = static_cast<std::uint32_t>(
              sample < 0 ? -sample : sample);
          normalizePeak_ = std::max(normalizePeak_, magnitude);
        }
      }
      bytesRemaining_ -= chunk;
      readOffset_ += chunk;
      completedWork_ += chunk;
      lastStepPayloadIoBytes_ += chunk;
    } else if (phase_ == ApplyPhase::CopyWorking) {
      const int bytesRead =
          readFile_->Read(scratch_.data(), static_cast<int>(chunk));
      if (bytesRead != static_cast<int>(chunk) ||
          writeFile_->Write(scratch_.data(), 1, bytesRead) != bytesRead)
        return FailApply(Ui2SampleEditorTransactionResult::CopyFailed);
      bytesRemaining_ -= chunk;
      readOffset_ += chunk;
      writeOffset_ += chunk;
      completedWork_ += chunk;
      lastStepPayloadIoBytes_ += chunk * 2U;
    } else if (phase_ == ApplyPhase::TrimMove) {
      readFile_->Seek(static_cast<long>(readOffset_), SEEK_SET);
      const int bytesRead =
          readFile_->Read(scratch_.data(), static_cast<int>(chunk));
      if (bytesRead != static_cast<int>(chunk))
        return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
      readFile_->Seek(static_cast<long>(writeOffset_), SEEK_SET);
      if (readFile_->Write(scratch_.data(), 1, bytesRead) != bytesRead)
        return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
      bytesRemaining_ -= chunk;
      readOffset_ += chunk;
      writeOffset_ += chunk;
      completedWork_ += chunk;
      lastStepPayloadIoBytes_ += chunk * 2U;
    } else if (phase_ == ApplyPhase::NormalizeWrite) {
      readFile_->Seek(static_cast<long>(readOffset_), SEEK_SET);
      const int bytesRead =
          readFile_->Read(scratch_.data(), static_cast<int>(chunk));
      if (bytesRead != static_cast<int>(chunk))
        return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
      if (applyHeader_.bitsPerSample == 8U) {
        for (std::uint32_t index = 0U; index < chunk; ++index) {
          const std::int32_t centered =
              static_cast<std::int32_t>(scratch_[index]) - 128;
          std::int64_t scaled =
              static_cast<std::int64_t>(centered) * normalizeGainQ16_;
          scaled += scaled >= 0 ? 0x8000 : -0x8000;
          scaled >>= 16U;
          scratch_[index] = static_cast<std::uint8_t>(
              std::clamp<std::int64_t>(scaled, -128, 127) + 128);
        }
      } else {
        for (std::uint32_t index = 0U; index < chunk; index += 2U) {
          const std::uint16_t encoded =
              static_cast<std::uint16_t>(scratch_[index]) |
              static_cast<std::uint16_t>(scratch_[index + 1U] << 8U);
          const std::int32_t sample = static_cast<std::int16_t>(encoded);
          std::int64_t scaled =
              static_cast<std::int64_t>(sample) * normalizeGainQ16_;
          scaled += scaled >= 0 ? 0x8000 : -0x8000;
          scaled >>= 16U;
          const std::int16_t output = static_cast<std::int16_t>(
              std::clamp<std::int64_t>(scaled, -32768, 32767));
          const std::uint16_t outputBits = static_cast<std::uint16_t>(output);
          scratch_[index] = static_cast<std::uint8_t>(outputBits);
          scratch_[index + 1U] =
              static_cast<std::uint8_t>(outputBits >> 8U);
        }
      }
      readFile_->Seek(static_cast<long>(readOffset_), SEEK_SET);
      if (readFile_->Write(scratch_.data(), 1, bytesRead) != bytesRead)
        return FailApply(Ui2SampleEditorTransactionResult::MutationFailed);
      bytesRemaining_ -= chunk;
      readOffset_ += chunk;
      completedWork_ += chunk;
      lastStepPayloadIoBytes_ += chunk * 2U;
    }

    UpdateProgress();
    if (bytesRemaining_ != 0U)
      continue;
    Ui2SampleEditorTransactionResult transition =
        phase_ == ApplyPhase::NormalizeScan
            ? CompleteNormalizeScan()
            : phase_ == ApplyPhase::CopyWorking
                  ? CompleteCopy()
                  : phase_ == ApplyPhase::TrimMove
                        ? CompleteTrim()
                        : CompleteNormalizeWrite();
    if (transition != Ui2SampleEditorTransactionResult::InProgress)
      return transition;
  }
  return Ui2SampleEditorTransactionResult::InProgress;
}

Ui2SampleEditorTransactionResult
Ui2SampleEditorTransaction::ApplyTrim(std::uint32_t start,
                                      std::uint32_t end) {
  Ui2SampleEditorTransactionResult result = BeginTrim(start, end);
  while (result == Ui2SampleEditorTransactionResult::InProgress)
    result = StepApply(DefaultStepBudget);
  return result;
}

Ui2SampleEditorTransactionResult Ui2SampleEditorTransaction::ApplyNormalize() {
  Ui2SampleEditorTransactionResult result = BeginNormalize();
  while (result == Ui2SampleEditorTransactionResult::InProgress)
    result = StepApply(DefaultStepBudget);
  return result;
}

} // namespace ui2
