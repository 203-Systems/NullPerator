#include "System.h"
#include "Adapters/node/audio/Audio.h"
#include "Adapters/node/gui/GUIFactory.h"
#include "Adapters/node/timer/Timer.h"
#ifdef DUMMY_MIDI
#include "Adapters/Dummy/Midi/DummyMidi.h"
#else
#include "Adapters/node/midi/MidiService.h"
#endif
#include "Application/Commands/NodeList.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Player/SyncMaster.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "Adapters/node/filesystem/FileSystem.h"
#include "Adapters/node/hal/nullperator/power/power.h"
#include "SamplePool.h"
#include "input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include <assert.h>
#include <fcntl.h>
#include <malloc.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "Adapters/node/platform/platform.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <cmath>

EventManager *NodeSystem::eventManager_ = NULL;
bool NodeSystem::invert_ = false;

int NodeSystem::MainLoop() {

  eventManager_->InstallMappings();
  return eventManager_->MainLoop();
};

void NodeSystem::Boot(int argc, char **argv) {

  // Install System
  alignas(NodeSystem) static char systemMemBuf[sizeof(NodeSystem)];
  System::Install(new (systemMemBuf) NodeSystem());

  // Install GUI Factory
  alignas(GUIFactory) static char guiMemBuf[sizeof(GUIFactory)];
  I_GUIWindowFactory::Install(new (guiMemBuf) GUIFactory());

  // Install Timers
  alignas(NodeTimerService) static char timerMemBuf[sizeof(NodeTimerService)];
  TimerService::GetInstance()->Install(new (timerMemBuf)
                                           NodeTimerService());

  // Install FileSystem
  alignas(NodeFileSystem) static char fsMemBuf[sizeof(NodeFileSystem)];
  FileSystem::Install(new (fsMemBuf) NodeFileSystem());

  auto fs = FileSystem::GetInstance();
  if (!fs->chdir("/")) {
    Trace::Log("NODESYSTEM", "SDCARD MISSING!!");
  }

  // Install MIDI
  // **NOTE**: MIDI install MUST happen before Audio install because it triggers
  // reading config file and config file needs to have MidiService already
  // installed in order to apply midi settings read from the config file
#ifdef DUMMY_MIDI
  alignas(DummyMidi) static char midiMemBuf[sizeof(DummyMidi)];
  MidiService::Install(new (midiMemBuf) DummyMidi());
#else
  alignas(NodeMidiService) static char midiMemBuf[sizeof(NodeMidiService)];
  MidiService::Install(new (midiMemBuf) NodeMidiService());
#endif

  // Install Sound
  AudioSettings hint;
  hint.bufferSize_ = 1024;
  hint.preBufferCount_ = 8;
  alignas(NodeAudio) static char audioMemBuf[sizeof(NodeAudio)];
  Audio::Install(new (audioMemBuf) NodeAudio(hint));

  // Install SamplePool
  void *samplePoolMem = nullptr;
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
    samplePoolMem =
        heap_caps_malloc(sizeof(NodeSamplePool), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (samplePoolMem == nullptr) {
    samplePoolMem = heap_caps_malloc(sizeof(NodeSamplePool), MALLOC_CAP_8BIT);
  }
  assert(samplePoolMem != nullptr);
  SamplePool::Install(new (samplePoolMem) NodeSamplePool());

  eventManager_ = I_GUIWindowFactory::GetInstance()->GetEventManager();
  eventManager_->Init();
};

void NodeSystem::Shutdown() {
  Audio *audio = Audio::GetInstance();
  if (audio != nullptr) {
    // NodeAudio is placement-new'd into static storage during initialization.
    // Close its hardware resources without attempting to free that storage.
    audio->Close();
  }
};

unsigned long NodeSystem::GetClock() { return Millis(); }

void NodeSystem::GetBatteryState(BatteryState &state) {
  const float voltage = NullperatorHAL::Power::GetBatteryVoltage();
  if (voltage <= 0.0f) {
    state.percentage = 0;
    state.voltage_mv = 0;
    state.temperature_c = 0;
    state.charging = false;
    state.error = true;
    return;
  }

  state.percentage = NullperatorHAL::Power::GetBatteryPercentage();
  state.voltage_mv = static_cast<uint16_t>(std::lround(voltage * 1000.0f));
  state.temperature_c = 0;
  state.charging = NullperatorHAL::Power::IsCharging();
  state.error = false;
}

void NodeSystem::Sleep(int millisec) {
  if (millisec <= 0) {
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(millisec));
}

void NodeSystem::PostQuitMessage() { eventManager_->PostQuitMessage(); }

unsigned int NodeSystem::GetMemoryUsage() {
  struct mallinfo m = mallinfo();
  return m.uordblks;
}

void NodeSystem::SetDisplayBrightness(unsigned char value) {
  platform_brightness(value);
}

void NodeSystem::PowerDown() { enter_sleep(); }

void NodeSystem::SystemBootloader() { SystemReboot(); }

void NodeSystem::SystemReboot() { esp_restart(); }

void NodeSystem::SystemPutChar(int c) { putchar(c); }

uint32_t NodeSystem::GetRandomNumber() { return esp_random(); }

uint32_t NodeSystem::Micros() {
  return static_cast<uint32_t>(esp_timer_get_time());
}

uint32_t NodeSystem::Millis() { return Micros() / 1000; }
