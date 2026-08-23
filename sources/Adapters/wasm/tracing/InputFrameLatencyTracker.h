/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef PICOTRACKER_WASM_INPUT_FRAME_LATENCY_TRACKER_H
#define PICOTRACKER_WASM_INPUT_FRAME_LATENCY_TRACKER_H

#include <cstddef>
#include <cstdint>

// Correlates a browser input press with the first committed display frame that
// contains work performed by the corresponding DispatchEvent. The
// implementation is a fixed-capacity atomic slot table: it never allocates
// and does not make the browser producer wait for the application pthread.
class InputFrameLatencyTracker final {
public:
  static constexpr std::size_t Capacity = 16;
  static constexpr std::uint64_t NoPresentationTimeoutUs = 2'000'000U;

  // Called when InputMap accepts a new browser press into desiredActions. This
  // is deliberately before SDL queueing so queue retries are part of the
  // end-to-end measurement.
  static std::uint16_t AcceptPress(std::uint16_t action) noexcept;

  // Withdraws a browser press that was coalesced or released before SDL ever
  // accepted its DOWN transition. The correlation token makes this precise
  // even when an older press of the same action is already queued.
  static void CancelAccepted(std::uint16_t correlation) noexcept;

  // Called on the application pthread after SDL dequeues the transition and
  // immediately before the active C++ view handles DispatchEvent. Views may
  // commit their response from inside DispatchEvent, so marking after it
  // returns would miss the actual first presented frame.
  static void MarkDispatching(std::uint16_t action, bool pressed) noexcept;

  // Called only after emscripten_webgl_commit_frame reports success. All
  // dispatched presses are correlated with this frame and receive their own
  // latency counter. The fixed slot capacity bounds the records per commit.
  static void PresentedFrame() noexcept;

  // Reclaims tickets that never reach a committed frame. This is safe to call
  // after a skipped or failed presentation and when accepting later input.
  static void ObserveNoPresentation() noexcept;

#ifdef HOST_TEST
  static void ResetForTesting() noexcept;
  static std::size_t PendingForTesting() noexcept;
  static std::uint64_t OverflowForTesting() noexcept;
#endif
};

#endif
