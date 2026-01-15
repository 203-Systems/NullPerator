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
  uint8_t midicmd[3] = {0, 0, 0};

  midicmd[0] = msg.status_;
  if (msg.status_ < 0xF0) {
    midicmd[1] = msg.data1_;
    midicmd[2] = msg.data2_;
    sendUSBMidiMessage(midicmd, 3);
  } else {
    sendUSBMidiMessage(midicmd, 1);
  }
}
