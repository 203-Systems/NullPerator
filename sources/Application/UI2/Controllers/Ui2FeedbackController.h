/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace ui2 {

// Explicit, transient feedback for operations that otherwise fail silently.
//
// Unlike lifecycle dialogs, this controller has no input handler: the active
// page keeps ownership of every button while the feedback overlay is visible.
// Text and timing state are stored inline so publishing and capture never
// allocate. Legacy Status messages are deliberately not consumed here; each
// operation that deserves visible feedback must opt in with Show*().
class Ui2FeedbackController final {
public:
  static constexpr std::uint32_t MessageDurationMs = 1800U;
  static constexpr std::uint32_t ErrorDurationMs = 3000U;

  [[nodiscard]] bool Active() const { return active_; }
  [[nodiscard]] std::uint32_t InstanceId() const { return instanceId_; }

  void ShowMessage(const char *text, std::uint32_t nowMs) {
    Show(UiDialogTone::Message, text, nowMs, MessageDurationMs);
  }

  void ShowError(const char *text, std::uint32_t nowMs) {
    Show(UiDialogTone::Error, text, nowMs, ErrorDurationMs);
  }

  // Returns true only when expiry changed visible state, allowing the owner to
  // invalidate the retained renderer without polling snapshot contents.
  bool Tick(std::uint32_t nowMs) {
    if (!active_ || nowMs - shownAtMs_ < durationMs_)
      return false;
    Clear();
    return true;
  }

  void Clear() {
    active_ = false;
    text_.fill('\0');
  }

  [[nodiscard]] Ui2DialogSnapshot Snapshot() const {
    Ui2DialogSnapshot snapshot;
    snapshot.kind = UiDialogKind::Feedback;
    snapshot.tone = tone_;
    snapshot.SetTitle(std::string_view(text_.data(), TextLength()));
    return snapshot;
  }

private:
  void Show(UiDialogTone tone, const char *text, std::uint32_t nowMs,
            std::uint32_t durationMs) {
    text_.fill('\0');
    if (text != nullptr) {
      const std::string_view source{text};
      const std::size_t count =
          std::min(source.size(), text_.size() - 1U);
      std::copy_n(source.begin(), count, text_.begin());
    }
    if (text_[0] == '\0') {
      active_ = false;
      return;
    }
    tone_ = tone;
    shownAtMs_ = nowMs;
    durationMs_ = durationMs;
    active_ = true;
    ++instanceId_;
  }

  [[nodiscard]] std::size_t TextLength() const {
    const auto end = std::find(text_.begin(), text_.end(), '\0');
    return static_cast<std::size_t>(end - text_.begin());
  }

  std::array<char, Ui2DialogSnapshot::TextCapacity> text_{};
  std::uint32_t shownAtMs_ = 0U;
  std::uint32_t durationMs_ = 0U;
  std::uint32_t instanceId_ = 0U;
  UiDialogTone tone_ = UiDialogTone::Message;
  bool active_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2FeedbackController>);
static_assert(sizeof(Ui2FeedbackController) <= 56U,
              "feedback state must remain fixed and embedded-friendly");

} // namespace ui2
