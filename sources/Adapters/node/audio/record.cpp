/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2025
 *
 * This file is part of the esp32 firmware
 */

#include "record.h"

#include "System/Console/Trace.h"

void Record(void * /*argument*/) {}

bool StartRecording(const char * /*filename*/, uint8_t /*threshold*/,
                    uint32_t /*milliseconds*/) {
  Trace::Log("ESP32", "Recording not implemented on ESP32");
  return false;
}

void StopRecording() {}

void RequestStopRecording() {}

bool WaitForRecordingStop(uint32_t /*timeoutMs*/) { return true; }

void FinishStopRecording() {}

void StartMonitoring() {}

void StopMonitoring() {}

void SetInputSource(RecordSource /*source*/) {}

void SetLineInGain(uint8_t /*gainDb*/) {}

void SetMicGain(uint8_t /*gainDb*/) {}

bool IsRecordingActive() { return false; }

bool IsSavingRecording() { return false; }

uint8_t GetSavingProgressPercent() { return 0; }

bool DidLastRecordingCaptureAudio() { return false; }
