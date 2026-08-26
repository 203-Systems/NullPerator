/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2StatusBridge.h"

#include <cstring>

namespace ui2 {

Ui2StatusBridge::~Ui2StatusBridge() { Detach(); }

void Ui2StatusBridge::Attach() {
  if (attached_)
    return;

  forwardSink_ = Status::GetInstance();
  if (forwardSink_ == this)
    forwardSink_ = nullptr;
  Status::Install(this);
  attached_ = true;
}

void Ui2StatusBridge::Detach() {
  if (!attached_)
    return;

  // Do not overwrite a sink installed by another owner after us. In the usual
  // UI2-only lifecycle this is still `this`, so the previous legacy/reference
  // sink (or nullptr) is restored deterministically.
  if (Status::GetInstance() == this)
    Status::Install(forwardSink_);
  forwardSink_ = nullptr;
  attached_ = false;
}

void Ui2StatusBridge::Clear() {
  text_.fill('\0');
  layout_ = Ui2StatusLayout::SingleLine;
  hasValue_ = false;
  ++revision_;
}

Ui2StatusSnapshot Ui2StatusBridge::Read() const {
  Ui2StatusSnapshot snapshot{};
  snapshot.text = text_;
  snapshot.revision = revision_;
  snapshot.layout = layout_;
  snapshot.hasValue = hasValue_;
  return snapshot;
}

void Ui2StatusBridge::Print(char *text) {
  Capture(text, Ui2StatusLayout::SingleLine);
  if (forwardSink_ != nullptr)
    forwardSink_->Print(text);
}

void Ui2StatusBridge::PrintMultiLine(char *text) {
  Capture(text, Ui2StatusLayout::MultiLine);
  if (forwardSink_ != nullptr)
    forwardSink_->PrintMultiLine(text);
}

void Ui2StatusBridge::Capture(const char *text, Ui2StatusLayout layout) {
  text_.fill('\0');
  if (text != nullptr) {
    std::strncpy(text_.data(), text, text_.size() - 1U);
    text_.back() = '\0';
  }
  layout_ = layout;
  hasValue_ = true;
  ++revision_;
}

} // namespace ui2
