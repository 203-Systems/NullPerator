#ifndef _NODEAUDIO_H_
#define _NODEAUDIO_H_

#include "Services/Audio/Audio.h"

class NodeAudio : public Audio {
public:
  NodeAudio(AudioSettings &hints);
  ~NodeAudio();
  virtual void Init();
  virtual void Close();
  virtual int GetMixerVolume();
  virtual void SetMixerVolume(int volume);

private:
  AudioSettings hints_;
};
#endif
