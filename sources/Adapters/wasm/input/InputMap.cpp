/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/wasm/input/InputMap.h"
#include "Adapters/wasm/tracing/InputFrameLatencyTracker.h"

#ifndef HOST_TEST
#include <SDL.h>
#endif

#include <array>
#include <mutex>

namespace {
constexpr std::uint16_t ActionCount =
    InputMap::ActionCount;
static_assert(ActionCount <= 16, "The application action mask is 16 bits");

std::mutex actionMutex;
std::uint16_t heldActions = 0;
// desiredActions records the latest browser intent. heldActions advances only
// once the matching SDL user event has been accepted, so retrying cannot put an
// UP ahead of its DOWN even when browser calls race one another.
std::uint16_t desiredActions = 0;
std::uint16_t dispatchedActions = 0;
std::uint32_t dispatchGeneration = 0;
std::uint16_t lastDispatchedAction = ActionCount;
std::array<std::uint16_t, ActionCount> pendingTraceCorrelations{};

std::uintptr_t EncodeAction(std::uint16_t action, bool pressed) {
  return (static_cast<std::uintptr_t>(action) << 1u) | (pressed ? 1u : 0u);
}

bool SdlQueueAction(std::uint16_t action, bool pressed) {
#ifdef HOST_TEST
  (void)action;
  (void)pressed;
  return false;
#else
  SDL_Event event{};
  event.type = SDL_USEREVENT;
  event.user.code = InputMap::ActionEventCode;
  event.user.data1 = reinterpret_cast<void *>(EncodeAction(action, pressed));
  return SDL_PushEvent(&event) == 1;
#endif
}

InputMap::QueueActionFunction queueAction = SdlQueueAction;

void RetryPendingTransitionsLocked() {
  for (std::uint16_t action = 0; action < ActionCount; ++action) {
    const std::uint16_t bit = static_cast<std::uint16_t>(1u << action);
    const bool isHeld = (heldActions & bit) != 0;
    const bool shouldBeHeld = (desiredActions & bit) != 0;
    if (isHeld == shouldBeHeld) {
      // A desired press can disappear before SDL sees it: either its pending
      // queue attempt was released, or a failed release was pressed again
      // while the application still considered the action held. Retire only
      // that exact browser correlation instead of letting a later SDL DOWN
      // inherit its older timestamp.
      if (pendingTraceCorrelations[action] != 0U) {
        InputFrameLatencyTracker::CancelAccepted(
            pendingTraceCorrelations[action]);
        pendingTraceCorrelations[action] = 0U;
      }
      continue;
    }
    if (!queueAction(action, shouldBeHeld)) {
      continue;
    }
    if (shouldBeHeld) {
      heldActions |= bit;
      pendingTraceCorrelations[action] = 0U;
    } else {
      heldActions &= static_cast<std::uint16_t>(~bit);
    }
  }
}
} // namespace

bool InputMap::SetAction(std::uint16_t action, bool pressed) {
  if (!TrackerActionIdIsValid(action)) {
    return false;
  }

  const std::uint16_t bit = static_cast<std::uint16_t>(1u << action);
  std::lock_guard<std::mutex> lock(actionMutex);
  const bool wasDesired = (desiredActions & bit) != 0;
  if (pressed && !wasDesired) {
    // Acceptance into desiredActions is the browser-side boundary. SDL queue
    // retries remain inside the end-to-end latency instead of disappearing
    // into the much narrower InputDispatch scope.
    pendingTraceCorrelations[action] =
        InputFrameLatencyTracker::AcceptPress(action);
  }
  if (pressed) {
    desiredActions |= bit;
  } else {
    desiredActions &= static_cast<std::uint16_t>(~bit);
  }
  RetryPendingTransitionsLocked();
  return ((heldActions & bit) != 0) == pressed;
}

bool InputMap::RepeatAction(std::uint16_t action) {
  if (!TrackerActionIdIsValid(action))
    return false;
  const TrackerAction semantic = static_cast<TrackerAction>(action);
  if (semantic != TrackerAction::Left && semantic != TrackerAction::Down &&
      semantic != TrackerAction::Right && semantic != TrackerAction::Up)
    return false;

  const std::uint16_t bit = static_cast<std::uint16_t>(1u << action);
  std::lock_guard<std::mutex> lock(actionMutex);
  if ((desiredActions & bit) == 0U || (heldActions & bit) == 0U)
    return false;
  return queueAction(action, true);
}

void InputMap::ReleaseAllActions() {
  std::lock_guard<std::mutex> lock(actionMutex);
  desiredActions = 0;
  RetryPendingTransitionsLocked();
}

void InputMap::RetryPendingTransitions() {
  std::lock_guard<std::mutex> lock(actionMutex);
  RetryPendingTransitionsLocked();
}

std::uint16_t InputMap::GetHeldActionMask() {
  std::lock_guard<std::mutex> lock(actionMutex);
  return heldActions;
}

std::uint16_t InputMap::GetDispatchedActionMask() {
  std::lock_guard<std::mutex> lock(actionMutex);
  return dispatchedActions;
}

std::uint32_t InputMap::GetDispatchGeneration() {
  std::lock_guard<std::mutex> lock(actionMutex);
  return dispatchGeneration;
}

std::uint16_t InputMap::GetLastDispatchedAction() {
  std::lock_guard<std::mutex> lock(actionMutex);
  return lastDispatchedAction;
}

void InputMap::AcknowledgeAction(std::uint16_t action, bool pressed) {
  if (!TrackerActionIdIsValid(action)) {
    return;
  }
  const std::uint16_t bit = static_cast<std::uint16_t>(1u << action);
  std::lock_guard<std::mutex> lock(actionMutex);
  if (pressed) {
    dispatchedActions |= bit;
  } else {
    dispatchedActions &= static_cast<std::uint16_t>(~bit);
  }
  lastDispatchedAction = action;
  ++dispatchGeneration;
}

bool InputMap::DecodeActionEvent(std::uintptr_t encoded, std::uint16_t &action,
                                 bool &pressed) {
  action = static_cast<std::uint16_t>(encoded >> 1u);
  pressed = (encoded & 1u) != 0;
  if (!TrackerActionIdIsValid(action)) {
    return false;
  }
  // DecodeActionEvent runs on the application pthread immediately before
  // WasmEventManager invokes DispatchEvent. A view may Flush synchronously
  // from that call, so this is the last correct point to arm presentation.
  InputFrameLatencyTracker::MarkDispatching(action, pressed);
  return true;
}

void InputMap::SetQueueForTesting(QueueActionFunction queue) {
  std::lock_guard<std::mutex> lock(actionMutex);
  queueAction = queue == nullptr ? SdlQueueAction : queue;
}

void InputMap::ResetQueueForTesting() {
  std::lock_guard<std::mutex> lock(actionMutex);
  queueAction = SdlQueueAction;
}
