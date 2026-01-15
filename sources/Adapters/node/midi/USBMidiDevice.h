#ifndef _NODEUSBMIDIDEVICE_H_
#define _NODEUSBMIDIDEVICE_H_

#include "Services/Midi/MidiOutDevice.h"

class NodeUSBMidiOutDevice : public MidiOutDevice {
public:
  NodeUSBMidiOutDevice(const char *name);
  virtual bool Init();
  virtual void Close();
  virtual bool Start();
  virtual void Stop();

protected:
  virtual void SendMessage(MidiMessage &);

private:
};
#endif
