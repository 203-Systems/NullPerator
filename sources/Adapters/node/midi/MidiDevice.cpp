
#include "MidiDevice.h"
#include "Adapters/node/hal/nullperator/midi/midi.h"

NodeMidiOutDevice::NodeMidiOutDevice(const char *name)
    : MidiOutDevice(name) {}
bool NodeMidiOutDevice::Init() { return true; }

void NodeMidiOutDevice::Close(){};

bool NodeMidiOutDevice::Start() { return true; };

void NodeMidiOutDevice::Stop() {}

void NodeMidiOutDevice::SendMessage(MidiMessage &msg) {
  const MidiWireMessage encoded = EncodeMidiWireMessage(msg);
  (void)NullperatorHAL::MIDI::Send(encoded.bytes.data(), encoded.length);
}
