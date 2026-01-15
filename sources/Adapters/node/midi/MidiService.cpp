
#include "MidiService.h"
#include "MidiDevice.h"
#include "USBMidiDevice.h"

NodeMidiService::NodeMidiService(){};

NodeMidiService::~NodeMidiService(){};

void NodeMidiService::buildDriverList() {
  // create a midi device for each of Midi Output device
  MidiOutDevice *dev = new NodeMidiOutDevice("MIDI OUT 1");
  outList_.insert(outList_.end(), dev);
  dev = new NodeUSBMidiOutDevice("USB");
  outList_.insert(outList_.end(), dev);
};
