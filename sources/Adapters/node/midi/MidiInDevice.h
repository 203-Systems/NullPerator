#ifndef _NODEMIDIINDEVICE_H_
#define _NODEMIDIINDEVICE_H_

#include "Services/Midi/MidiInDevice.h"

class NodeMidiInDevice : public MidiInDevice {
public:
  NodeMidiInDevice(const char *name);
  ~NodeMidiInDevice() override;

  void poll() override;

  bool Start() override;
  void Stop() override;

protected:
  bool initDriver() override;
  void closeDriver() override;
  bool startDriver() override;
  void stopDriver() override;
};

#endif
