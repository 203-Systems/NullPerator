#ifndef _NODEMIDIDEVICE_H_
#define _NODEMIDIDEVICE_H_

#include "Services/Midi/MidiOutDevice.h"

class NodeMidiOutDevice : public MidiOutDevice {
public:
  NodeMidiOutDevice(const char *name);
  virtual bool Init();
  virtual void Close();
  virtual bool Start();
  virtual void Stop();

protected:
  virtual void SendMessage(MidiMessage &);

private:
};
#endif
