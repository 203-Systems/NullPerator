/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmDisconnectedMidi.h"

#include "Services/Midi/MidiInDevice.h"
#include "Services/Midi/MidiOutDevice.h"
#include "System/Console/Trace.h"

namespace {
class DisconnectedMidiInput final : public MidiInDevice {
public:
  DisconnectedMidiInput() : MidiInDevice("Disconnected") {}

  bool Start() override { return MidiInDevice::Start(); }
  void Stop() override { MidiInDevice::Stop(); }
  void poll() override {}

protected:
  bool initDriver() override { return true; }
  void closeDriver() override {}
  bool startDriver() override { return true; }
  void stopDriver() override {}
};

class DisconnectedMidiOutput final : public MidiOutDevice {
public:
  explicit DisconnectedMidiOutput(const char *name) : MidiOutDevice(name) {}

  bool Init() override { return true; }
  void Close() override {}
  bool Start() override { return true; }
  void Stop() override {}
  void SendMessage(MidiMessage &) override {}
};
} // namespace

WasmDisconnectedMidi::WasmDisconnectedMidi() {
  static DisconnectedMidiInput input;
  static DisconnectedMidiOutput output("MIDI disconnected");
  static DisconnectedMidiOutput usbOutput("USB MIDI disconnected");
  inList_.push_back(&input);
  outList_.push_back(&output);
  outList_.push_back(&usbOutput);
  Trace::Log("WASM_MIDI", "disconnected MIDI adapter installed");
}
