/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2DeviceLifecycleController.h"
#include "System/System/System.h"

#include <cstdint>
#include <type_traits>

namespace ui2 {

// This result describes dispatch, not bootloader success. SystemBootloader()
// has a void legacy contract, so UI2 cannot observe whether a platform accepted
// the request. In WASM the adapter safely stays in-process and records the
// unsupported request through WasmProcess instead of pretending to reboot.
enum class Ui2DeviceLifecycleDispatch : std::uint8_t {
  Ignored,
  SystemUnavailable,
  RequestDispatched,
};

// Allocation-free System boundary, injectable in focused tests. Applications
// should execute only the command returned after the explicit YES selection.
class Ui2DeviceLifecycleService {
public:
  constexpr Ui2DeviceLifecycleService() = default;

  [[nodiscard]] static constexpr Ui2DeviceLifecycleService
  FromSystem(System *system) {
    return Ui2DeviceLifecycleService(system);
  }

  [[nodiscard]] Ui2DeviceLifecycleDispatch
  Execute(Ui2DeviceLifecycleCommand command) const {
    if (command.type != Ui2DeviceLifecycleCommandType::EnterBootloader)
      return Ui2DeviceLifecycleDispatch::Ignored;
    if (system_ == nullptr)
      return Ui2DeviceLifecycleDispatch::SystemUnavailable;
    system_->SystemBootloader();
    return Ui2DeviceLifecycleDispatch::RequestDispatched;
  }

private:
  explicit constexpr Ui2DeviceLifecycleService(System *system)
      : system_(system) {}

  System *system_ = nullptr;
};

static_assert(std::is_trivially_copyable_v<Ui2DeviceLifecycleService>);
static_assert(sizeof(Ui2DeviceLifecycleService) <= sizeof(void *));

} // namespace ui2
