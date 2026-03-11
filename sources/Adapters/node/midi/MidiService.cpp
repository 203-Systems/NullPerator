
#include "MidiService.h"

NodeMidiService::NodeMidiService()
    : midiOutDevice_("MIDI OUT"), usbMidiOutDevice_("USB"),
      midiInDevice_("MIDI IN") {
  outList_.insert(outList_.end(), &midiOutDevice_);
  outList_.insert(outList_.end(), &usbMidiOutDevice_);
  inList_.insert(inList_.end(), &midiInDevice_);
}

NodeMidiService::~NodeMidiService(){};

void NodeMidiService::poll() { midiInDevice_.poll(); }
