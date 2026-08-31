/* SPDX-License-Identifier: BSD-3-Clause */

#include "Application/Audio/RecordingPlatform.h"

// Input recording is intentionally a separate adapter milestone. Keeping the
// firmware hooks here makes the complete application link without pretending
// that an iOS microphone stream is already active.
void SetInputSource(RecordSource) {}
void SetLineInGain(std::uint8_t) {}
void SetMicGain(std::uint8_t) {}
bool IsRecordingAvailable() { return false; }
bool IsRecordingActive() { return false; }
bool IsSavingRecording() { return false; }
void Record(void *) {}
bool StartRecording(const char *, std::uint8_t, std::uint32_t) { return false; }
void StopRecording() {}
void RequestStopRecording() {}
bool WaitForRecordingStop(std::uint32_t) { return true; }
void FinishStopRecording() {}
void StartMonitoring() {}
void StopMonitoring() {}
std::uint8_t GetSavingProgressPercent() { return 0; }
bool DidLastRecordingCaptureAudio() { return false; }
