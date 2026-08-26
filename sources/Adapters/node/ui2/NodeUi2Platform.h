/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Adapters/node/ui2/NodeUi2InputMailbox.h"
#include "Application/UI2/Ui2TrackerApplication.h"
#include "UI2/Render/IUiPresenter.h"
#include "UI2/Render/UiRgb565Presenter.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

// UI2-only Node/ESP32-S3 task and display boundary. Exactly one application
// task constructs, mutates, renders, and destroys Ui2TrackerApplication. The
// input task only publishes sampled physical state; the USB task only services
// TinyUSB. Legacy AppWindow/GUIWindow objects are not part of this graph.
class NodeUi2Platform final : public ui2::IUiPresenter {
public:
  enum class State : std::uint8_t {
    Stopped,
    Starting,
    Running,
    Stopping,
    Failed,
  };

  static constexpr std::uint32_t kInputScanMs = 10U;
  static constexpr std::uint32_t kUsbServiceMs = 1U;
  static constexpr std::uint32_t kFramePeriodMs = 33U;
  static constexpr std::size_t kApplicationStorageBytes =
      sizeof(ui2::Ui2TrackerApplication);
  static constexpr std::size_t kApplicationStorageAlignment =
      alignof(ui2::Ui2TrackerApplication);

  // UI2 currently has no device-side Remote UI raster protocol. The old Node
  // Remote UI streams character cells and cannot represent the indexed UI2
  // surface, anti-aliased cursors, or page transitions. Product cutover must
  // leave that option unavailable until a bounded RGB565/indexed transport is
  // designed; it must not run the legacy renderer beside this presenter.
  static constexpr bool kRemoteUiRasterSupported = false;

  // Application storage is caller-owned so the small cross-core task control
  // object can remain in internal DRAM while the large fixed UI/model block is
  // deliberately placed in PSRAM. It must stay alive until all tasks stop.
  NodeUi2Platform(void *applicationStorage,
                  std::size_t applicationStorageBytes);
  ~NodeUi2Platform() override;

  NodeUi2Platform(const NodeUi2Platform &) = delete;
  NodeUi2Platform &operator=(const NodeUi2Platform &) = delete;

  // Native Node services (System/FileSystem/MIDI/Audio/SamplePool) and hardware
  // must already be installed by the UI2-only boot path. Do not call legacy
  // NodeSystem::Boot just for this class: it constructs GUIFactory/EventManager.
  // State::Running means Ui2TrackerApplication::Init completed on its owner.
  [[nodiscard]] bool Start(ui2::Ui2StartupOptions startup = {});
  void RequestStop();
  // Wait until every task has published its stopped bit. Callers must complete
  // this join before destroying the control object or freeing application
  // storage. UINT32_MAX waits indefinitely.
  [[nodiscard]] bool WaitForStop(std::uint32_t timeoutMs);

  [[nodiscard]] State CurrentState() const {
    return state_.load(std::memory_order_acquire);
  }

  ui2::PresentResult
  Present(const ui2::UiIndexedSurface &surface, const ui2::UiPalette &palette,
          std::span<const ui2::DirtyStrip> strips) override;

private:
  static constexpr std::uint32_t kApplicationTaskStackBytes = 16U * 1024U;
  static constexpr std::uint32_t kInputTaskStackBytes = 4U * 1024U;
  static constexpr std::uint32_t kUsbTaskStackBytes = 2U * 1024U;
  static constexpr EventBits_t kApplicationStoppedBit = BIT0;
  static constexpr EventBits_t kInputStoppedBit = BIT1;
  static constexpr EventBits_t kUsbStoppedBit = BIT2;
  static constexpr EventBits_t kInputPublishedBit = BIT3;
  static constexpr EventBits_t kAllStoppedBits =
      kApplicationStoppedBit | kInputStoppedBit | kUsbStoppedBit;

  static void ApplicationTaskEntry(void *context);
  static void InputTaskEntry(void *context);
  static void UsbTaskEntry(void *context);
  static bool WriteRgb565Chunk(void *context, std::uint16_t x,
                               std::uint16_t y, std::uint16_t width,
                               std::uint16_t height,
                               const std::uint16_t *pixels);

  void RunApplicationTask();
  void RunInputTask();
  void RunUsbTask();
  void PublishInputSample(std::uint16_t heldMask, bool headphoneConnected,
                          std::uint32_t nowMs);
  [[nodiscard]] node::ui2::InputMailbox::Batch DrainInput();
  [[nodiscard]] std::uint16_t LatestInputMask();
  void DispatchInput(ui2::Ui2TrackerApplication &application,
                     const node::ui2::InputMailbox::Batch &batch);
  void ApplyHeadphoneRoute(bool connected);
  void PollMidi();
  void MarkTaskStopped(EventBits_t bit);

  void *applicationStorage_ = nullptr;
  std::size_t applicationStorageBytes_ = 0U;
  ui2::Ui2TrackerApplication *application_ = nullptr;
  node::ui2::InputMailbox inputMailbox_{};
  portMUX_TYPE inputMux_ = portMUX_INITIALIZER_UNLOCKED;
  ui2::UiRgb565Presenter rgb565Presenter_;
  ui2::Ui2StartupOptions startup_{};
  std::atomic<State> state_{State::Stopped};
  std::atomic<bool> runRequested_{false};
  StaticEventGroup_t taskEventsStorage_{};
  EventGroupHandle_t taskEvents_ = nullptr;
};

static_assert(ui2::UiRgb565Presenter::kChunkRows == 8U);
static_assert(ui2::UiRgb565Presenter::kTransferPixels == 240U * 8U);
