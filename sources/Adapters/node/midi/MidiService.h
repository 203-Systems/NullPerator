#ifndef _NODEMIDISERVICE_H_
#define _NODEMIDISERVICE_H_

#include "Services/Midi/MidiService.h"
#include "MidiDevice.h"
#include "MidiInDevice.h"
#include "USBMidiDevice.h"

class NodeMidiService : public MidiService {
public:
  NodeMidiService();
  ~NodeMidiService();

  void poll();

private:
  NodeMidiOutDevice midiOutDevice_;
  NodeUSBMidiOutDevice usbMidiOutDevice_;
  NodeMidiInDevice midiInDevice_;
};

#endif
