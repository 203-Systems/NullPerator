/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 *
 * This file is part of the picoTracker firmware
 */
#ifndef _RECORDING_PLATFORM_H_
#define _RECORDING_PLATFORM_H_

#include <cstdint>

// This is the application-facing ABI for the recording backend. Platform
// adapters provide these existing free-function symbols; application code must
// not include an adapter-specific record.h just to access them.
// Keep the numeric values aligned with Config::recordSourceOptions_.
enum RecordSource { LineIn, OnboardMic, HeadphoneMic };
static_assert(LineIn == 0 && OnboardMic == 1 && HeadphoneMic == 2,
              "RecordSource values are persisted");

void SetInputSource(RecordSource source);
// Some hosts expose one system-managed input route instead of a product-level
// source selector. The UI uses this capability to omit source editing while
// keeping recording itself available.
bool IsRecordingInputSelectable();
// Capability is owned by the platform adapter so UI/application code does not
// infer support from target names or from gain ranges. Backends must keep this
// false until the complete record transaction is implemented.
bool IsRecordingAvailable();
bool IsRecordingActive();
bool IsMonitoringActive();
bool IsSavingRecording();
void Record(void *argument);
bool StartRecording(const char *filename, std::uint8_t threshold,
                    std::uint32_t milliseconds);
void StopRecording();
void RequestStopRecording();
bool WaitForRecordingStop(std::uint32_t timeoutMs);
void FinishStopRecording();
void StartMonitoring();
void StopMonitoring();
std::uint8_t GetSavingProgressPercent();
bool DidLastRecordingCaptureAudio();
std::uint32_t GetRecordingElapsedMilliseconds();
std::uint16_t GetRecordingInputPeak();

#endif
