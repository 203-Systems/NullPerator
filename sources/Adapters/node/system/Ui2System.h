/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "System/System/System.h"

#include <atomic>
#include <cstdint>

class NodeSamplePool;

// Service-only System implementation for the strict Node UI2 product.  It
// installs the native filesystem/audio/MIDI/timer/sample services without
// constructing GUIFactory, EventManager, AppWindow, or a legacy character
// framebuffer.
class NodeUi2System final : public System {
public:
  [[nodiscard]] static bool Boot(int argc, char **argv);
  static void Shutdown();

  // The panel is initialized with its backlight off. Ui2TrackerApplication may
  // restore the configured brightness while it is still loading; keep that
  // value cached until the first complete RGB565 frame has reached the panel.
  static void RevealDisplay();
  [[nodiscard]] static bool QuitRequested();

  unsigned long GetClock() override;
  void GetBatteryState(BatteryState &state) override;
  void SetDisplayBrightness(unsigned char value) override;
  void PostQuitMessage() override;
  unsigned int GetMemoryUsage() override;
  void PowerDown() override;
  void SystemBootloader() override;
  void SystemReboot() override;
  void SystemPutChar(int c) override;
  std::uint32_t GetRandomNumber() override;
  std::uint32_t Micros() override;
  std::uint32_t Millis() override;

private:
  static std::atomic<bool> quitRequested_;
  static std::uint8_t requestedBrightness_;
  static bool displayRevealed_;
  static bool booted_;
  static NodeSamplePool *samplePool_;
};
