/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>

enum class WasmAudioState : std::uint32_t {
  Unavailable = 0,
  Locked = 1,
  Starting = 2,
  Running = 3,
  Suspended = 4,
  Failed = 5,
  Stopped = 6,
};

// This is the stable C/JS metrics copy contract.  Keep all fields 32-bit so a
// browser can copy it safely from shared Wasm memory without a torn 64-bit
// read. Counters intentionally saturate rather than wrap in practical use.
struct WasmAudioMetrics {
  static constexpr std::uint32_t Version = 4;

  std::uint32_t version = Version;
  std::uint32_t size = sizeof(WasmAudioMetrics);
  std::uint32_t state = 0;
  std::uint32_t ringFillFrames = 0;
  std::uint32_t ringCapacityFrames = 0;
  std::uint32_t renderMicros = 0;
  std::uint32_t underrunFrames = 0;
  std::uint32_t overrunFrames = 0;
  std::uint32_t sourceRate = 44100;
  std::uint32_t destinationRate = 0;
  // Incremented once for every successful real AudioWorklet process callback.
  std::uint32_t callbackCount = 0;
  // Non-realtime setup diagnostics. They make a browser failure actionable
  // without adding logging or stateful work to the process callback.
  std::uint32_t setupPhase = 0;
  std::uint32_t unlockOnBrowserMainThread = 0;
};

static_assert(sizeof(WasmAudioMetrics) == 52U,
              "The JavaScript metrics copy contract is intentionally fixed");
