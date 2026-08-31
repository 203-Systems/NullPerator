#include "USBMidiDevice.h"
#include "Adapters/node/platform/platform.h"
#include "System/Console/Trace.h"
#include <stdlib.h>
#include "usb_utils.h"

NodeUSBMidiOutDevice::NodeUSBMidiOutDevice(const char *name)
    : MidiOutDevice(name) {}

bool NodeUSBMidiOutDevice::Init() { return true; }

void NodeUSBMidiOutDevice::Close(){};

bool NodeUSBMidiOutDevice::Start() { return true; };

void NodeUSBMidiOutDevice::Stop() {}

void NodeUSBMidiOutDevice::SendMessage(MidiMessage &msg) {
  const MidiWireMessage encoded = EncodeMidiWireMessage(msg);
  sendUSBMidiMessage(encoded.bytes.data(), encoded.length);
}
