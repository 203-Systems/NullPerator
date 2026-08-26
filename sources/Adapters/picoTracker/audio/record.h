/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef _RECORD_H_
#define _RECORD_H_

#include "Application/Audio/RecordingPlatform.h"

// DUMMY values as no linein or mic on pico
#define LINEIN_GAIN_MINDB RecordingPlatform::kLineInGainMinDb
#define LINEIN_GAIN_MAXDB RecordingPlatform::kLineInGainMaxDb
#define MIC_GAIN_MINDB RecordingPlatform::kMicGainMinDb
#define MIC_GAIN_MAXDB RecordingPlatform::kMicGainMaxDb

void Record(void *);
bool StartRecording(const char *filename, uint8_t threshold,
                    uint32_t milliseconds);
void StopRecording();
void RequestStopRecording();
bool WaitForRecordingStop(uint32_t timeoutMs);
void FinishStopRecording();
void StartMonitoring();
void StopMonitoring();
uint8_t GetSavingProgressPercent();
// True only if the last recording captured samples and was persisted to disk.
bool DidLastRecordingCaptureAudio();

#endif
