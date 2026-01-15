#ifndef _NODEMIDISERVICE_H_
#define _NODEMIDISERVICE_H_

#include "Services/Midi/MidiService.h"

class NodeMidiService : public MidiService {
public:
  NodeMidiService();
  ~NodeMidiService();

protected:
  virtual void buildDriverList();
};

#endif
