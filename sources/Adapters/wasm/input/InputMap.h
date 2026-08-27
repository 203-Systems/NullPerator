/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Application/Input/TrackerInput.h"

#include <cstdint>

class InputMap final {
public:
  // Browser action ids are the product's semantic TrackerAction ids. Keeping
  // a second legacy GUIEvent-shaped enum here previously made ids 4..7 look
  // like ALT/EDIT/ENTER/NAV even though the native application consumes
  // SHIFT/OPTION/EDIT/PLAY.
  static constexpr std::uint16_t ActionCount =
      static_cast<std::uint16_t>(TrackerAction::Count);
  static constexpr int ActionEventCode = 2;
  using QueueActionFunction = bool (*)(std::uint16_t action, bool pressed);

  static bool SetAction(std::uint16_t action, bool pressed);
  // Queues another DOWN edge for an already-held direction without changing
  // physical hold state. This mirrors the Node mailbox repeat contract.
  static bool RepeatAction(std::uint16_t action);
  static void ReleaseAllActions();
  // Retries action transitions that SDL could not accept on an earlier frame.
  // WasmEventManager calls this from its frame pump while the runtime is ready.
  static void RetryPendingTransitions();
  static std::uint16_t GetHeldActionMask();
  static std::uint16_t GetDispatchedActionMask();
  static std::uint32_t GetDispatchGeneration();
  static std::uint16_t GetLastDispatchedAction();
  static void AcknowledgeAction(std::uint16_t action, bool pressed);
  static bool DecodeActionEvent(std::uintptr_t encoded, std::uint16_t &action,
                                bool &pressed);

  // Host tests inject a deterministic queue. Keeping this narrow hook also
  // makes a queue failure observable without fabricating a UI event.
  static void SetQueueForTesting(QueueActionFunction queue);
  static void ResetQueueForTesting();
};
