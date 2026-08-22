/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>

// These values deliberately match GUIEventPadButtonType. Keeping the browser
// API independent of SDL key codes means all browser input follows the same
// pad action path as the physical tracker controls.
enum class WasmAction : std::uint16_t {
  Left = 0,
  Down,
  Right,
  Up,
  Alt,
  Edit,
  Enter,
  Nav,
  Play,
  Select,
  Power,
  Count,
};

class InputMap final {
public:
  static constexpr int ActionEventCode = 2;
  using QueueActionFunction = bool (*)(std::uint16_t action, bool pressed);

  static bool SetAction(std::uint16_t action, bool pressed);
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
