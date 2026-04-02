
#include "MidiDevice.h"
#include "Adapters/node/hal/nullperator/midi/midi.h"

NodeMidiOutDevice::NodeMidiOutDevice(const char *name)
    : MidiOutDevice(name) {}
bool NodeMidiOutDevice::Init() { return true; }

void NodeMidiOutDevice::Close(){};

bool NodeMidiOutDevice::Start() { return true; };

void NodeMidiOutDevice::Stop() {}

void NodeMidiOutDevice::SendMessage(MidiMessage &msg) {
  uint8_t bytes[3] = {msg.status_, msg.data1_, msg.data2_};
  size_t length = 1;

  if (msg.status_ < 0xF0) {
    length = (msg.data2_ == MidiMessage::UNUSED_BYTE) ? 2 : 3;
  }
  (void)NullperatorHAL::MIDI::Send(bytes, length);
}
