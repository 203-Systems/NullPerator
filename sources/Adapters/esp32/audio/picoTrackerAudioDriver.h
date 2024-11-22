#ifndef _PICOTRACKERAUDIO_DRIVER_H_
#define _PICOTRACKERAUDIO_DRIVER_H_

#include "Foundation/T_Singleton.h"
#include "Services/Audio/AudioDriver.h"
#include "freeRTOS/FreeRTOS.h"
#include "freeRTOS/task.h"
#include "driver/i2s_std.h"


#define MINI_BLANK_SIZE 128 // Samples

class picoTrackerAudioDriver : public AudioDriver {
public:
  picoTrackerAudioDriver(AudioSettings &settings);
  virtual ~picoTrackerAudioDriver();

  // Sound implementation
  virtual bool InitDriver();
  virtual void CloseDriver();
  virtual bool StartDriver();
  virtual void StopDriver();
  virtual int GetPlayedBufferPercentage();
  virtual int GetSampleRate() { return 44100; };
  virtual bool Interlaced() { return true; };

  // Additional
  void OnChunkDone();
  void SetVolume(int v);
  int GetVolume();
  virtual double GetStreamTime();
  static void IRQHandler();
  static void BufferNeeded();

private:
  static bool i2s_tx_done_callback(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx);
  static void AudioThread(void* arg);
  static picoTrackerAudioDriver *instance_;
  AudioSettings settings_;
  static const char miniBlank_[MINI_BLANK_SIZE * 2 * sizeof(short)];
  int volume_;
  uint32_t startTime_;
};
#endif
