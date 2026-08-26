/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2025
 *
 * This file is part of the esp32 firmware
 */

#ifndef _ESP32_RECORD_H_
#define _ESP32_RECORD_H_

#include "Application/Audio/RecordingPlatform.h"

// DUMMY values until node recording backend exists.
#define LINEIN_GAIN_MINDB RecordingPlatform::kLineInGainMinDb
#define LINEIN_GAIN_MAXDB RecordingPlatform::kLineInGainMaxDb
#define MIC_GAIN_MINDB RecordingPlatform::kMicGainMinDb
#define MIC_GAIN_MAXDB RecordingPlatform::kMicGainMaxDb

void Record(void *argument);
bool StartRecording(const char *filename, uint8_t threshold,
                    uint32_t milliseconds);
void StopRecording();
void RequestStopRecording();
bool WaitForRecordingStop(uint32_t timeoutMs);
void FinishStopRecording();
void StartMonitoring();
void StopMonitoring();
uint8_t GetSavingProgressPercent();
bool DidLastRecordingCaptureAudio();

#endif
