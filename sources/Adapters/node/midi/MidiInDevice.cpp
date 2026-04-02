#include "MidiInDevice.h"
#include "Adapters/node/hal/nullperator/midi/midi.h"

namespace {
constexpr uint32_t kPollTimeoutMs = 0;
constexpr size_t kPollBufferSize = 32;
}

NodeMidiInDevice::NodeMidiInDevice(const char *name) : MidiInDevice(name) {}

NodeMidiInDevice::~NodeMidiInDevice() { closeDriver(); }

bool NodeMidiInDevice::initDriver() { return true; }

void NodeMidiInDevice::closeDriver() {}

bool NodeMidiInDevice::Start() { return MidiInDevice::Start(); }

void NodeMidiInDevice::Stop() { MidiInDevice::Stop(); }

bool NodeMidiInDevice::startDriver() { return true; }

void NodeMidiInDevice::stopDriver() {}

void NodeMidiInDevice::poll() {
  if (!IsRunning()) {
    return;
  }

  uint8_t buffer[kPollBufferSize];
  while (true) {
    const int bytesRead = NullperatorHAL::MIDI::Receive(
        buffer, sizeof(buffer), kPollTimeoutMs);
    if (bytesRead <= 0) {
      return;
    }

    for (int i = 0; i < bytesRead; ++i) {
      processMidiData(buffer[i]);
    }
  }
}
