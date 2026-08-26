/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "System/io/Status.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace ui2 {

enum class Ui2StatusLayout : std::uint8_t {
  SingleLine,
  MultiLine,
};

// A by-value view of the latest legacy Status message. The revision changes
// for every publish and Clear(), allowing a platform or future UI controller
// to poll without retaining pointers into the bridge.
struct Ui2StatusSnapshot final {
  std::array<char, Status::kTextCapacity> text{};
  std::uint32_t revision = 0U;
  Ui2StatusLayout layout = Ui2StatusLayout::SingleLine;
  bool hasValue = false;
};

// Captures the process-global Status channel for an UI2-only application.
//
// This class intentionally does not decide whether a status belongs in a
// modal, top bar, bottom bar, or progress screen. It only retains the latest
// value in fixed storage and forwards it to the sink that was installed before
// Attach(), preserving legacy AppWindow behavior in mixed/reference builds.
// Like the pre-existing Status factory, publishing and reading are confined to
// the application thread; this bridge adds no lock or hidden allocation.
class Ui2StatusBridge final : public Status {
public:
  Ui2StatusBridge() = default;
  ~Ui2StatusBridge() override;

  Ui2StatusBridge(const Ui2StatusBridge &) = delete;
  Ui2StatusBridge &operator=(const Ui2StatusBridge &) = delete;

  void Attach();
  void Detach();
  void Clear();

  [[nodiscard]] Ui2StatusSnapshot Read() const;
  [[nodiscard]] bool Attached() const { return attached_; }
  [[nodiscard]] bool HasValue() const { return hasValue_; }
  [[nodiscard]] std::uint32_t Revision() const { return revision_; }

  void Print(char *text) override;
  void PrintMultiLine(char *text) override;

private:
  void Capture(const char *text, Ui2StatusLayout layout);

  Status *forwardSink_ = nullptr;
  std::array<char, Status::kTextCapacity> text_{};
  std::uint32_t revision_ = 0U;
  Ui2StatusLayout layout_ = Ui2StatusLayout::SingleLine;
  bool attached_ = false;
  bool hasValue_ = false;
};

static_assert(sizeof(Ui2StatusBridge) <= 160U,
              "status bridge must remain fixed and embedded-friendly");
static_assert(std::is_trivially_copyable_v<Ui2StatusSnapshot>,
              "status snapshots cross the platform boundary by value");
static_assert(sizeof(Ui2StatusSnapshot) <= 136U,
              "status snapshots must remain fixed and embedded-friendly");

} // namespace ui2
