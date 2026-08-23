/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Header-light control boundary for the strict -Werror exported bridge.
 */

#pragma once

#include "WasmAudioState.h"

void WasmAudio_BootstrapBrowserMain() noexcept;
void WasmAudio_MarkUnavailable() noexcept;
bool WasmAudio_Unlock() noexcept;
void WasmAudio_Stop() noexcept;
void WasmAudio_MarkRunning() noexcept;
WasmAudioState WasmAudio_GetState() noexcept;
const char *WasmAudio_GetError() noexcept;
const WasmAudioMetrics *WasmAudio_CopyMetrics() noexcept;
const std::uint32_t *WasmAudio_MetricsSnapshotAddress() noexcept;
const std::uint32_t *WasmAudio_ErrorSnapshotAddress() noexcept;
