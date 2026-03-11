/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2025
 *
 * This file is part of the esp32 firmware
 */

#ifndef _ESP32_RECORD_H_
#define _ESP32_RECORD_H_

#include <cstdint>

// DUMMY values until node recording backend exists.
#define LINEIN_GAIN_MINDB 0
#define LINEIN_GAIN_MAXDB 0
#define MIC_GAIN_MINDB 0
#define MIC_GAIN_MAXDB 0

enum RecordSource { AllOff, LineIn, Mic, USBIn };

void Record(void *argument);
bool StartRecording(const char *filename, uint8_t threshold,
                    uint32_t milliseconds);
void StopRecording();
void RequestStopRecording();
bool WaitForRecordingStop(uint32_t timeoutMs);
void FinishStopRecording();
void StartMonitoring();
void StopMonitoring();
void SetInputSource(RecordSource source);
void SetLineInGain(uint8_t gainDb);
void SetMicGain(uint8_t gainDb);
bool IsRecordingActive();
bool IsSavingRecording();
uint8_t GetSavingProgressPercent();
bool DidLastRecordingCaptureAudio();

#endif
