/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Adapters/node/ui2/NodeUi2Platform.h"

#include "Adapters/node/display/Rgb565DisplayTransport.h"
#include "Adapters/node/hal/nullperator/input/input.h"
#include "Adapters/node/midi/MidiService.h"
#include "Adapters/node/platform/platform.h"
#include "Adapters/node/system/Ui2System.h"
#include "Services/Midi/MidiService.h"

#include "esp_attr.h"
#include "esp_log.h"

#include <algorithm>
#include <memory>

namespace {

constexpr char kLogTag[] = "NODE_UI2";

// Static internal DRAM is DMA-capable on ESP32-S3. UI2 converts at most 24
// 240-pixel rows at a time and the synchronous display transport completes the
// transfer before this buffer is reused; there is no second 240x240 RGB frame.
DMA_ATTR std::uint16_t
    gUi2TransferPixels[ui2::UiRgb565Presenter::kTransferPixels]{};

[[nodiscard]] bool TimeReached(std::uint32_t nowMs,
                               std::uint32_t targetMs) {
  return static_cast<std::int32_t>(nowMs - targetMs) >= 0;
}

[[nodiscard]] TickType_t WaitTicksUntil(std::uint32_t nowMs,
                                        std::uint32_t targetMs) {
  if (TimeReached(nowMs, targetMs))
    return 0U;
  const std::uint32_t remainingMs = targetMs - nowMs;
  return std::max<TickType_t>(1U, pdMS_TO_TICKS(remainingMs));
}

[[nodiscard]] std::uint16_t ReadPhysicalHeldMask(bool *headphoneConnected) {
  const NullperatorHAL::Input::ButtonState_t buttons =
      NullperatorHAL::Input::GetButtonState(headphoneConnected);
  std::uint16_t mask = 0U;
  const auto set = [&mask](TrackerAction action, bool held) {
    if (held)
      mask |= TrackerActionBit(action);
  };
  set(TrackerAction::Left, buttons.left);
  set(TrackerAction::Down, buttons.down);
  set(TrackerAction::Right, buttons.right);
  set(TrackerAction::Up, buttons.up);
  set(TrackerAction::Play, buttons.select);
  set(TrackerAction::Option, buttons.b);
  // The current HAL deliberately reports A=false for a board-level stuck-key
  // workaround. Keep the semantic mapping here; restoring the physical EDIT
  // source belongs in HAL and must not be hidden by a platform remap.
  set(TrackerAction::Edit, buttons.a);
  set(TrackerAction::Shift, buttons.start);
  set(TrackerAction::Power, buttons.func);
  return mask;
}

} // namespace

NodeUi2Platform::NodeUi2Platform(void *applicationStorage,
                                 std::size_t applicationStorageBytes)
    : applicationStorage_(applicationStorage),
      applicationStorageBytes_(applicationStorageBytes),
      rgb565Presenter_(gUi2TransferPixels,
                       ui2::UiRgb565Presenter::kTransferPixels,
                       &NodeUi2Platform::WriteRgb565Chunk, this,
                       ui2::UiRgb565ByteOrder::MostSignificantByteFirst,
                       ui2::UiRgb565ToneCurve::St7789Contrast) {
  taskEvents_ = xEventGroupCreateStatic(&taskEventsStorage_);
}

NodeUi2Platform::~NodeUi2Platform() {
  if (taskEvents_ != nullptr) {
    // The owner joins every task before destruction, so deleting this static
    // event-group control object cannot unblock code that still references the
    // platform. FreeRTOS recognizes the statically allocated backing store and
    // does not pass it to the heap allocator.
    vEventGroupDelete(taskEvents_);
    taskEvents_ = nullptr;
  }
}

bool NodeUi2Platform::Start(ui2::Ui2StartupOptions startup) {
  State expected = State::Stopped;
  if (!state_.compare_exchange_strong(expected, State::Starting,
                                      std::memory_order_acq_rel))
    return false;

  if (taskEvents_ == nullptr) {
    ESP_LOGE(kLogTag, "Unable to create task lifecycle event group");
    state_.store(State::Failed, std::memory_order_release);
    return false;
  }

  const std::uintptr_t storageAddress =
      reinterpret_cast<std::uintptr_t>(applicationStorage_);
  if (applicationStorage_ == nullptr ||
      applicationStorageBytes_ < kApplicationStorageBytes ||
      storageAddress % kApplicationStorageAlignment != 0U) {
    ESP_LOGE(kLogTag, "Invalid UI2 application storage");
    state_.store(State::Failed, std::memory_order_release);
    return false;
  }

  startup_ = startup;
  taskENTER_CRITICAL(&inputMux_);
  inputMailbox_ = {};
  taskEXIT_CRITICAL(&inputMux_);
  runRequested_.store(true, std::memory_order_release);
  display_rgb565_transport_init();
  xEventGroupSetBits(taskEvents_, kAllStoppedBits);
  xEventGroupClearBits(taskEvents_, kInputPublishedBit);

  xEventGroupClearBits(taskEvents_, kApplicationStoppedBit);
  if (xTaskCreatePinnedToCore(ApplicationTaskEntry, "UI2 Application",
                              kApplicationTaskStackBytes, this,
                              tskIDLE_PRIORITY + 1, nullptr, 0) !=
      pdPASS) {
    ESP_LOGE(kLogTag, "Failed to create UI2 application task");
    xEventGroupSetBits(taskEvents_, kApplicationStoppedBit);
    runRequested_.store(false, std::memory_order_release);
    state_.store(State::Failed, std::memory_order_release);
    return false;
  }

  xEventGroupClearBits(taskEvents_, kInputStoppedBit);
  if (xTaskCreatePinnedToCore(InputTaskEntry, "UI2 Input",
                              kInputTaskStackBytes, this,
                              tskIDLE_PRIORITY + 2, nullptr, 0) != pdPASS) {
    ESP_LOGE(kLogTag, "Failed to create UI2 input task");
    xEventGroupSetBits(taskEvents_, kInputStoppedBit);
    RequestStop();
    state_.store(State::Failed, std::memory_order_release);
    return false;
  }

  return true;
}

void NodeUi2Platform::RequestStop() {
  const State state = state_.load(std::memory_order_acquire);
  if (state == State::Stopped)
    return;
  if (state != State::Failed)
    state_.store(State::Stopping, std::memory_order_release);
  runRequested_.store(false, std::memory_order_release);
  // This bit is only a wakeup hint. The application task owns all mutable UI
  // state and observes runRequested_ before touching caller-owned storage.
  if (taskEvents_ != nullptr)
    xEventGroupSetBits(taskEvents_, kInputPublishedBit);
}

bool NodeUi2Platform::WaitForStop(std::uint32_t timeoutMs) {
  if (taskEvents_ == nullptr)
    return true;
  const TickType_t timeoutTicks =
      timeoutMs == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
  const EventBits_t bits = xEventGroupWaitBits(
      taskEvents_, kAllStoppedBits, pdFALSE, pdTRUE, timeoutTicks);
  return (bits & kAllStoppedBits) == kAllStoppedBits;
}

ui2::PresentResult NodeUi2Platform::Present(
    const ui2::UiIndexedSurface &surface, const ui2::UiPalette &palette,
    std::span<const ui2::DirtyStrip> strips) {
  return rgb565Presenter_.Present(surface, palette, strips);
}

bool NodeUi2Platform::WriteRgb565Chunk(
    void *, std::uint16_t x, std::uint16_t y, std::uint16_t width,
    std::uint16_t height, const std::uint16_t *pixels) {
  return display_draw_rgb565_region(x, y, width, height, pixels);
}

void NodeUi2Platform::ApplicationTaskEntry(void *context) {
  static_cast<NodeUi2Platform *>(context)->RunApplicationTask();
}

void NodeUi2Platform::InputTaskEntry(void *context) {
  static_cast<NodeUi2Platform *>(context)->RunInputTask();
}

void NodeUi2Platform::PublishInputSample(std::uint16_t heldMask,
                                         bool headphoneConnected,
                                         std::uint32_t nowMs) {
  taskENTER_CRITICAL(&inputMux_);
  inputMailbox_.PublishSample(heldMask, headphoneConnected, nowMs);
  taskEXIT_CRITICAL(&inputMux_);

  // The event bit is only a wake-up hint. All state needed to reconcile input
  // remains in the mailbox, so coalescing cannot lose an observed release or
  // overflow a FreeRTOS event queue. No TaskHandle crosses cores, avoiding the
  // notify-after-self-delete race of a raw task handle.
  xEventGroupSetBits(taskEvents_, kInputPublishedBit);
}

node::ui2::InputMailbox::Batch NodeUi2Platform::DrainInput() {
  taskENTER_CRITICAL(&inputMux_);
  node::ui2::InputMailbox::Batch batch = inputMailbox_.Drain();
  taskEXIT_CRITICAL(&inputMux_);
  return batch;
}

std::uint16_t NodeUi2Platform::LatestInputMask() {
  taskENTER_CRITICAL(&inputMux_);
  const std::uint16_t mask = inputMailbox_.LatestPhysicalHeldMask();
  taskEXIT_CRITICAL(&inputMux_);
  return mask;
}

void NodeUi2Platform::DispatchInput(
    ui2::Ui2TrackerApplication &application,
    const node::ui2::InputMailbox::Batch &batch) {
  for (std::size_t index = 0U; index < batch.size; ++index) {
    const node::ui2::InputMailbox::Event &event = batch.events[index];
    for (std::uint16_t count = 0U; count < event.count; ++count)
      application.DispatchTrackerAction(event.action, event.pressed);
  }
}

void NodeUi2Platform::ApplyHeadphoneRoute(bool connected) {
  const int volume = audio_codec_get_volume();
  (void)audio_codec_set_mute(true);
  switch_audio_mode(headphone_out);
  switch_speaker_mode(!connected);
  (void)audio_codec_set_volume(volume);
  ESP_LOGI(kLogTag, "Audio routed to %s",
           connected ? "headphones" : "speaker");
}

void NodeUi2Platform::PollMidi() {
#ifndef DUMMY_MIDI
  MidiService *service = MidiService::GetInstance();
  if (service != nullptr)
    static_cast<NodeMidiService *>(service)->poll();
#endif
}

void NodeUi2Platform::MarkTaskStopped(EventBits_t bit) {
  const EventBits_t bits = xEventGroupSetBits(taskEvents_, bit);
  if (((bits | bit) & kAllStoppedBits) == kAllStoppedBits &&
      state_.load(std::memory_order_acquire) != State::Failed) {
    state_.store(State::Stopped, std::memory_order_release);
  }
}

void NodeUi2Platform::RunApplicationTask() {
  // Wait for the input owner to publish a real boot sample. This both avoids a
  // fabricated all-up startup state and lets held M8 EDIT request the untitled
  // recovery project without involving legacy Application.cpp globals.
  while (runRequested_.load(std::memory_order_acquire)) {
    (void)xEventGroupWaitBits(taskEvents_, kInputPublishedBit, pdTRUE, pdFALSE,
                              portMAX_DELAY);
    taskENTER_CRITICAL(&inputMux_);
    const bool ready = inputMailbox_.HasPublishedSample();
    taskEXIT_CRITICAL(&inputMux_);
    if (ready)
      break;
  }

  if (!runRequested_.load(std::memory_order_acquire)) {
    MarkTaskStopped(kApplicationStoppedBit);
    vTaskDelete(nullptr);
    return;
  }

  application_ = std::construct_at(
      static_cast<ui2::Ui2TrackerApplication *>(applicationStorage_),
      *this);
  ui2::Ui2StartupOptions startup = startup_;
  startup.forceUntitledProject =
      startup.forceUntitledProject ||
      (LatestInputMask() & TrackerActionBit(TrackerAction::Edit)) != 0U;
  if (!application_->Init(startup)) {
    ESP_LOGE(kLogTag, "UI2 application initialization failed");
    std::destroy_at(application_);
    application_ = nullptr;
    runRequested_.store(false, std::memory_order_release);
    state_.store(State::Failed, std::memory_order_release);
    MarkTaskStopped(kApplicationStoppedBit);
    vTaskDelete(nullptr);
    return;
  }

  // A later task creation may have failed while Init was running. Do not let
  // this owner publish Running over the resulting Failed/Stopping state.
  if (!runRequested_.load(std::memory_order_acquire)) {
    std::destroy_at(application_);
    application_ = nullptr;
    MarkTaskStopped(kApplicationStoppedBit);
    vTaskDelete(nullptr);
    return;
  }

  state_.store(State::Running, std::memory_order_release);
  std::uint32_t nextFrameMs = millis();
  bool presenterFailureReported = false;

  while (runRequested_.load(std::memory_order_acquire)) {
    const std::uint32_t beforeWaitMs = millis();
    (void)xEventGroupWaitBits(taskEvents_, kInputPublishedBit, pdTRUE, pdFALSE,
                              WaitTicksUntil(beforeWaitMs, nextFrameMs));

    const node::ui2::InputMailbox::Batch input = DrainInput();
    if (input.headphoneChanged)
      ApplyHeadphoneRoute(input.headphoneConnected);
    DispatchInput(*application_, input);
    PollMidi();

    const std::uint32_t nowMs = millis();
    if (!TimeReached(nowMs, nextFrameMs))
      continue;

    application_->Tick(nowMs);
    const ui2::PresentResult result = application_->Present();
    if (result == ui2::PresentResult::Failed && !presenterFailureReported) {
      ESP_LOGE(kLogTag, "UI2 RGB565 presentation failed");
      presenterFailureReported = true;
    } else if (result == ui2::PresentResult::Presented) {
      presenterFailureReported = false;
      NodeUi2System::RevealDisplay();
    }

    nextFrameMs += kFramePeriodMs;
    if (TimeReached(nowMs, nextFrameMs + kFramePeriodMs))
      nextFrameMs = nowMs + kFramePeriodMs;
  }

  std::destroy_at(application_);
  application_ = nullptr;
  MarkTaskStopped(kApplicationStoppedBit);
  vTaskDelete(nullptr);
}

void NodeUi2Platform::RunInputTask() {
  TickType_t wake = xTaskGetTickCount();
  while (runRequested_.load(std::memory_order_acquire)) {
    bool headphoneConnected = false;
    const std::uint16_t heldMask =
        ReadPhysicalHeldMask(&headphoneConnected);
    PublishInputSample(heldMask, headphoneConnected, millis());
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(kInputScanMs));
  }
  MarkTaskStopped(kInputStoppedBit);
  vTaskDelete(nullptr);
}
