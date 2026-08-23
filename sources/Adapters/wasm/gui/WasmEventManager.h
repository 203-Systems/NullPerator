/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Foundation/T_Singleton.h"
#include "UIFramework/SimpleBaseClasses/EventManager.h"
#include "Application/UI2/Ui2ApplicationRuntime.h"

#include <atomic>
#include <optional>

class WasmEventManager final : public T_Singleton<WasmEventManager>,
                               public EventManager {
public:
  bool Init() override;
  int MainLoop() override;
  void PostQuitMessage() override;
  int GetKeyCode(const char *name) override;

  void RequestDiagnosticView(std::uint32_t viewType);
  void RequestDiagnosticModal(std::uint32_t modalType);
  std::uint32_t DiagnosticView() const;
  std::uint32_t DiagnosticViewGeneration() const;
  std::uint32_t DiagnosticModal() const;
  std::uint32_t DiagnosticModalGeneration() const;
  std::uint32_t DiagnosticInputGeneration() const;
  void SetUi2Enabled(bool enabled);
  bool Ui2Enabled() const;

private:
  static constexpr std::uint32_t NoDiagnosticView = 0xFFFFFFFFu;
  static constexpr std::uint32_t NoDiagnosticModal = 0xFFFFFFFFu;
  static void RunFrame(void *context);
  void PumpFrame();
  void StopRuntime();

  std::atomic<bool> finished_{false};
  bool runtimeStopped_ = false;
  bool booting_ = true;
  double nextTick_ = 0.0;
  std::atomic<std::uint32_t> requestedDiagnosticView_{NoDiagnosticView};
  std::atomic<std::uint32_t> diagnosticView_{NoDiagnosticView};
  std::atomic<std::uint32_t> diagnosticViewGeneration_{0};
  std::atomic<std::uint32_t> requestedDiagnosticModal_{NoDiagnosticModal};
  std::atomic<std::uint32_t> diagnosticModal_{NoDiagnosticModal};
  std::atomic<std::uint32_t> diagnosticModalGeneration_{0};
  std::atomic<std::uint32_t> diagnosticInputGeneration_{0};
  std::uint32_t diagnosticViewAwaitingDraw_ = NoDiagnosticView;
  std::uint32_t diagnosticModalAwaitingDraw_ = NoDiagnosticModal;
  std::atomic<bool> ui2Enabled_{false};
  std::optional<ui2::UiApplicationRuntime> ui2Runtime_;
};
