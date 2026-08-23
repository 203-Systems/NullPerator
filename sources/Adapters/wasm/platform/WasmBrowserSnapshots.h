/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>

// Stable pre-main descriptor. JavaScript obtains this once from
// onRuntimeInitialized, then reads the pointed-at shared Wasm memory with
// Atomics; it must not call the individual exports after PROXY_TO_PTHREAD.
struct WasmBrowserSnapshots {
  static constexpr std::uint32_t Version = 1U;
  std::uint32_t version = Version;
  std::uint32_t size = sizeof(WasmBrowserSnapshots);
  std::uint32_t frameData = 0U;
  std::uint32_t frameSequence = 0U;
  std::uint32_t audioMetrics = 0U;
  std::uint32_t audioError = 0U;
  std::uint32_t audioOracles = 0U;
};

static_assert(sizeof(WasmBrowserSnapshots) == 28U);

const WasmBrowserSnapshots *Wasm_BrowserSnapshots() noexcept;
const std::uint8_t *Wasm_FrameSnapshotAddress() noexcept;
const std::uint32_t *Wasm_FrameSequenceAddress() noexcept;
