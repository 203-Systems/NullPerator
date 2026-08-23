/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

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
  static constexpr std::uint32_t Version = 5;

  std::uint32_t version = Version;
  std::uint32_t size = sizeof(WasmAudioMetrics);
  std::uint32_t state = 0;
  std::uint32_t ringFillFrames = 0;
  std::uint32_t ringCapacityFrames = 0;
  // Duration of the latest application-thread producer request, from just
  // before ADET_BUFFERNEEDED observers run until their synchronous render and
  // ring-buffer write complete. This is not AudioWorklet callback time.
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
  // Latest and maximum successful AudioWorklet callback workload duration,
  // rounded up to an integral microsecond for display.
  // The clock sample and fixed lock-free atomics are the only instrumentation
  // executed by the realtime callback.
  std::uint32_t callbackMicros = 0;
  std::uint32_t callbackMaxMicros = 0;
  // ceil(actual callback frames * 1,000,000 / destinationRate). Zero means
  // the browser has not published a valid destination rate yet.
  std::uint32_t callbackDeadlineMicros = 0;
  // Successful callbacks whose unquantized workload duration exceeded the
  // above budget. Display rounding therefore cannot hide a sub-us overrun.
  std::uint32_t callbackDeadlineMisses = 0;
};

static_assert(std::is_standard_layout_v<WasmAudioMetrics>);
static_assert(sizeof(WasmAudioMetrics) == 68U,
              "The JavaScript metrics copy contract is intentionally fixed");
static_assert(offsetof(WasmAudioMetrics, version) == 0U);
static_assert(offsetof(WasmAudioMetrics, size) == 4U);
static_assert(offsetof(WasmAudioMetrics, state) == 8U);
static_assert(offsetof(WasmAudioMetrics, ringFillFrames) == 12U);
static_assert(offsetof(WasmAudioMetrics, ringCapacityFrames) == 16U);
static_assert(offsetof(WasmAudioMetrics, renderMicros) == 20U);
static_assert(offsetof(WasmAudioMetrics, underrunFrames) == 24U);
static_assert(offsetof(WasmAudioMetrics, overrunFrames) == 28U);
static_assert(offsetof(WasmAudioMetrics, sourceRate) == 32U);
static_assert(offsetof(WasmAudioMetrics, destinationRate) == 36U);
static_assert(offsetof(WasmAudioMetrics, callbackCount) == 40U);
static_assert(offsetof(WasmAudioMetrics, setupPhase) == 44U);
static_assert(offsetof(WasmAudioMetrics, unlockOnBrowserMainThread) == 48U);
static_assert(offsetof(WasmAudioMetrics, callbackMicros) == 52U);
static_assert(offsetof(WasmAudioMetrics, callbackMaxMicros) == 56U);
static_assert(offsetof(WasmAudioMetrics, callbackDeadlineMicros) == 60U);
static_assert(offsetof(WasmAudioMetrics, callbackDeadlineMisses) == 64U);
