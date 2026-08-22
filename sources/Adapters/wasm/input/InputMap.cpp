/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/wasm/input/InputMap.h"

#ifndef HOST_TEST
#include <SDL.h>
#endif

#include <mutex>

namespace {
constexpr std::uint16_t ActionCount =
    static_cast<std::uint16_t>(WasmAction::Count);
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

bool IsValidAction(std::uint16_t action) { return action < ActionCount; }

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
    if (isHeld == shouldBeHeld || !queueAction(action, shouldBeHeld)) {
      continue;
    }
    if (shouldBeHeld) {
      heldActions |= bit;
    } else {
      heldActions &= static_cast<std::uint16_t>(~bit);
    }
  }
}
} // namespace

bool InputMap::SetAction(std::uint16_t action, bool pressed) {
  if (!IsValidAction(action)) {
    return false;
  }

  const std::uint16_t bit = static_cast<std::uint16_t>(1u << action);
  std::lock_guard<std::mutex> lock(actionMutex);
  if (pressed) {
    desiredActions |= bit;
  } else {
    desiredActions &= static_cast<std::uint16_t>(~bit);
  }
  RetryPendingTransitionsLocked();
  return ((heldActions & bit) != 0) == pressed;
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
  if (!IsValidAction(action)) {
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
  return IsValidAction(action);
}

void InputMap::SetQueueForTesting(QueueActionFunction queue) {
  std::lock_guard<std::mutex> lock(actionMutex);
  queueAction = queue == nullptr ? SdlQueueAction : queue;
}

void InputMap::ResetQueueForTesting() {
  std::lock_guard<std::mutex> lock(actionMutex);
  queueAction = SdlQueueAction;
}
