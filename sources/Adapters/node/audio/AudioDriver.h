#ifndef _NODEAUDIO_DRIVER_H_
#define _NODEAUDIO_DRIVER_H_

#include "Foundation/T_Singleton.h"
#include "Services/Audio/AudioDriver.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstdint>


#define MINI_BLANK_SIZE 128 // Samples

class NodeAudioDriver : public AudioDriver {
public:
  NodeAudioDriver(AudioSettings &settings);
  virtual ~NodeAudioDriver();

  // Sound implementation
  virtual bool InitDriver();
  virtual void CloseDriver();
  virtual bool StartDriver();
  virtual void StopDriver();
  virtual int GetPlayedBufferPercentage();
  virtual int GetSampleRate() { return 44100; };
  virtual bool Interlaced() { return true; };
  void AddBuffer(short *buffer, int samplecount) override;

  // Additional
  void SetVolume(int v);
  int GetVolume();
  virtual double GetStreamTime();
  static void BufferNeeded();

private:
  static void AudioThread(void* arg);
  static void I2SThread(void* arg);
  static NodeAudioDriver *instance_;
  static uint8_t miniBlank_[MINI_BLANK_SIZE * 2U * sizeof(int16_t)];
  int volume_;
  // AudioDriver::isPlaying_ belongs to the application-facing base class.
  // Node's two worker tasks run on different cores, so their run gate needs a
  // real cross-core synchronization primitive instead of reading that plain
  // bool concurrently with Start()/Stop().
  std::atomic<bool> driverPlaying_{false};
  int renderBufferIndex_ = -1;
  bool renderBufferQueued_ = false;
  uint32_t startTime_;
};
#endif
