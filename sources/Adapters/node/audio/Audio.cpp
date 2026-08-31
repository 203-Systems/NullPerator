
#include "Audio.h"
#include "Services/Audio/AudioOutDriver.h"
#include "System/Console/Trace.h"
#include "AudioDriver.h"
#include <cstdint>

NodeAudio::NodeAudio(AudioSettings &hints) : Audio(hints) {}

NodeAudio::~NodeAudio() {}

void NodeAudio::Init() {
  AudioSettings settings;
  settings.audioAPI_ = GetAudioAPI();

  settings.bufferSize_ = GetAudioBufferSize();
  settings.preBufferCount_ = GetAudioPreBufferCount();

  alignas(NodeAudioDriver) static uint8_t audioDriver[sizeof(NodeAudioDriver)];
  NodeAudioDriver *drv = new (audioDriver) NodeAudioDriver(settings);

  alignas(AudioOutDriver) static uint8_t audioOutDriver[sizeof(AudioOutDriver)];
  AudioOutDriver *out = new (audioOutDriver) AudioOutDriver(*drv);
  AddOutput(*out);
}

void NodeAudio::Close() {
  for (auto *out : Outputs()) {
    if (out) {
      out->Close();
    }
  }
}

void NodeAudio::SetMixerVolume(int v) {
  AudioOutDriver *out = static_cast<AudioOutDriver *>(GetFirstOutput());
  if (out) {
    NodeAudioDriver *drv = static_cast<NodeAudioDriver *>(out->GetDriver());
    drv->SetVolume(v);
  }
}

int NodeAudio::GetMixerVolume() {
  AudioOutDriver *out = static_cast<AudioOutDriver *>(GetFirstOutput());
  if (out) {
    NodeAudioDriver *drv = static_cast<NodeAudioDriver *>(out->GetDriver());
    return drv->GetVolume();
  }
  return 0;
}
