
#include "Audio.h"
#include "Services/Audio/AudioOutDriver.h"
#include "System/Console/Trace.h"
#include "AudioDriver.h"

NodeAudio::NodeAudio(AudioSettings &hints) : Audio(hints) {
  hints_ = hints;
}

NodeAudio::~NodeAudio() {}

void NodeAudio::Init() {
  AudioSettings settings;
  settings.audioAPI_ = GetAudioAPI();

  settings.bufferSize_ = GetAudioBufferSize();
  settings.preBufferCount_ = GetAudioPreBufferCount();

  static char audioDriver[sizeof(NodeAudioDriver)];
  NodeAudioDriver *drv =
      new (audioDriver) NodeAudioDriver(settings);
  static char audioOutDriver[sizeof(AudioOutDriver)];
  AudioOutDriver *out = new (audioOutDriver) AudioOutDriver(*drv);
  Insert(out);
};

void NodeAudio::Close() {
  for (Begin(); !IsDone(); Next()) {
    AudioOut &current = CurrentItem();
    current.Close();
  }
};

void NodeAudio::SetMixerVolume(int v) {
  AudioOutDriver *out = (AudioOutDriver *)GetFirst();
  if (out) {
    NodeAudioDriver *drv = (NodeAudioDriver *)out->GetDriver();
    drv->SetVolume(v);
  }
}

int NodeAudio::GetMixerVolume() {
  AudioOutDriver *out = (AudioOutDriver *)GetFirst();
  if (out) {
    NodeAudioDriver *drv = (NodeAudioDriver *)out->GetDriver();
    return drv->GetVolume();
  }
  return 0;
}
