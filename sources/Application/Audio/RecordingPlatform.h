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
namespace RecordingPlatform {

constexpr int kLineInGainMinDb = 0;
constexpr int kLineInGainMaxDb = 0;
constexpr int kMicGainMinDb = 0;
constexpr int kMicGainMaxDb = 0;

} // namespace RecordingPlatform

// Keep the numeric values aligned with Config::recordSourceOptions_. Projects
// persist this value, so changing the order would break existing config files.
enum RecordSource { AllOff, LineIn, Mic, USBIn };
static_assert(AllOff == 0 && LineIn == 1 && Mic == 2 && USBIn == 3,
              "RecordSource values are persisted");

void SetInputSource(RecordSource source);
void SetLineInGain(std::uint8_t gainDb);
void SetMicGain(std::uint8_t gainDb);
// Capability is owned by the platform adapter so UI/application code does not
// infer support from target names or from gain ranges. Backends must keep this
// false until the complete record transaction is implemented.
bool IsRecordingAvailable();
bool IsRecordingActive();
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

#endif
