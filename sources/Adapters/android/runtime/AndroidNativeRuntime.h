/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class AndroidUiPresenter;
struct AndroidUiFramePacket;

namespace ui2 {
class Ui2TrackerApplication;
}

class AndroidNativeRuntime final {
public:
  static constexpr std::size_t FrameWidth = 240U;
  static constexpr std::size_t FrameHeight = 240U;

  struct MidiPacket {
    std::uint64_t sequence = 0U;
    std::array<std::uint8_t, 3> bytes{};
    std::uint8_t length = 0U;
  };
  struct MidiDrain {
    std::vector<MidiPacket> packets;
    std::uint32_t droppedNormal = 0U;
    std::uint32_t droppedRealtime = 0U;
  };

  explicit AndroidNativeRuntime(std::string documentsPath);
  ~AndroidNativeRuntime();

  AndroidNativeRuntime(const AndroidNativeRuntime &) = delete;
  AndroidNativeRuntime &operator=(const AndroidNativeRuntime &) = delete;

  bool Init();
  void Shutdown();
  void Suspend(bool suspended);
  void SetAction(std::uint8_t action, bool pressed, bool repeated);
  void ReleaseAllActions();
  void Tick();
  bool DrainFrame(std::uint32_t afterSequence, AndroidUiFramePacket &packet);
  void SetBattery(std::uint8_t percentage, bool charging, bool available);
  bool SubmitMidi(const std::uint8_t *bytes, std::size_t size,
                  double timestampMilliseconds);
  MidiDrain DrainMidi();
  void DisconnectMidi(std::uint32_t directions);
  void SetMidiOutputConnected(bool connected);
  static const char *BuildHash() noexcept;
  static const char *BuildTime() noexcept;

private:
  std::string documentsPath_;
  std::unique_ptr<AndroidUiPresenter> presenter_;
  std::unique_ptr<ui2::Ui2TrackerApplication> application_;
  std::uint16_t heldMask_ = 0U;
  bool servicesInstalled_ = false;
  bool initialized_ = false;
};
