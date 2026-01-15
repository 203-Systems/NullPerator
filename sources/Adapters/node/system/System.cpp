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
#include "Application/Controllers/ControlRoom.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Player/SyncMaster.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "Adapters/node/filesystem/FileSystem.h"
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
#include "esp_system.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_system.h"
#include "esp_timer.h"

EventManager *NodeSystem::eventManager_ = NULL;
bool NodeSystem::invert_ = false;

adc_oneshot_unit_handle_t adc_handle;
adc_cali_handle_t adc_cali_handle;
static bool adc_calibration_enabled = false;

int NodeSystem::MainLoop() {

  eventManager_->InstallMappings();
  return eventManager_->MainLoop();
};

void NodeSystem::Boot(int argc, char **argv) {

  // Install System
  static char systemMemBuf[sizeof(NodeSystem)];
  System::Install(new (systemMemBuf) NodeSystem());

  // Install GUI Factory
  static char guiMemBuf[sizeof(GUIFactory)];
  I_GUIWindowFactory::Install(new (guiMemBuf) GUIFactory());

  // Install Timers
  static char timerMemBuf[sizeof(NodeTimerService)];
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
  static char midiMemBuf[sizeof(DummyMidi)];
  MidiService::Install(new (midiMemBuf) DummyMidi());
#else
  static char midiMemBuf[sizeof(NodeMidiService)];
  MidiService::Install(new (midiMemBuf) NodeMidiService());
#endif

  // Install Sound
  AudioSettings hint;
  hint.bufferSize_ = 1024;
  hint.preBufferCount_ = 8;
  static char audioMemBuf[sizeof(NodeAudio)];
  Audio::Install(new (audioMemBuf) NodeAudio(hint));

  // Install SamplePool
  alignas(NodeSamplePool) static char
      samplePoolMemBuf[sizeof(NodeSamplePool)];
  SamplePool::Install(new (samplePoolMemBuf) NodeSamplePool());

  eventManager_ = I_GUIWindowFactory::GetInstance()->GetEventManager();
  eventManager_->Init();

  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = ADC_UNIT_1,
    };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

  adc_oneshot_chan_cfg_t oneshot_config = {
            .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, BATT_VOLTAGE_ADC_CHANNEL, &oneshot_config));

  adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .chan = BATT_VOLTAGE_ADC_CHANNEL,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
  esp_err_t cal_result = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
  if (cal_result == ESP_OK) {
    adc_calibration_enabled = true;
  } else {
    adc_calibration_enabled = false;
    Trace::Log("NODESYSTEM", "ADC calibration unavailable (err=0x%x), using raw readings", cal_result);
  }

  Trace::Log("NODESYSTEM", "ADC INIT DONE\n");
};

void NodeSystem::Shutdown() { delete Audio::GetInstance(); };

unsigned long NodeSystem::GetClock() { return Millis(); }

void NodeSystem::GetBatteryState(BatteryState &state) {
  int adc_raw = 0;
  int voltage = 0;
  esp_err_t read_err =
      adc_oneshot_read(adc_handle, BATT_VOLTAGE_ADC_CHANNEL, &adc_raw);
  esp_err_t cal_err = ESP_OK;
  if (read_err == ESP_OK && adc_calibration_enabled) {
    cal_err = adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage);
  } else if (read_err == ESP_OK) {
    // Approximate mV from raw value when calibration is unavailable.
    voltage = adc_raw * 1100 / 4095; // fallback for 12-bit, default Vref ~1.1V
  }

  if ((read_err != ESP_OK) || (cal_err != ESP_OK)) {
    state.percentage = 0;
    state.voltage_mv = 0;
    state.temperature_c = 0;
    state.charging = false;
    state.error = true;
    return;
  }

  voltage *= 2; // voltage divider reverse

  uint8_t percentage = 0;
  if (voltage >= 4200) {
    percentage = 100;
  } else if (voltage <= 3300) {
    percentage = 0;
  } else {
    percentage = static_cast<uint8_t>((voltage - 3300) * 100 / (4200 - 3300));
  }

  state.percentage = percentage;
  state.voltage_mv = static_cast<uint16_t>(voltage);
  state.temperature_c = 0;
  state.charging = false;
  state.error = false;
}

void NodeSystem::Sleep(int millisec) {
  if (millisec <= 0) {
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(millisec));
}

// NOTE: Legacy heap helpers were removed from the `System` interface.
// Keep these here for reference in case we reintroduce a platform allocator API.
#if 0
void *NodeSystem::Malloc(unsigned size) { return malloc(size); }
void NodeSystem::Free(void *ptr) { free(ptr); }
void NodeSystem::Memset(void *addr, char val, int size) { memset(addr, val, size); }
void *NodeSystem::Memcpy(void *s1, const void *s2, int n) { return memcpy(s1, s2, n); }
#endif

void NodeSystem::PostQuitMessage() { eventManager_->PostQuitMessage(); }

unsigned int NodeSystem::GetMemoryUsage() {
  struct mallinfo m = mallinfo();
  return m.uordblks;
}

void NodeSystem::SetDisplayBrightness(unsigned char value) {
  (void)value;
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
