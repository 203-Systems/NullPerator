#include "picoTrackerSystem.h"
#include "Adapters/esp32/audio/picoTrackerAudio.h"
#include "Adapters/esp32/gui/GUIFactory.h"
#include "Adapters/esp32/timer/picoTrackerTimer.h"
#ifdef DUMMY_MIDI
#include "Adapters/Dummy/Midi/DummyMidi.h"
#else
#include "Adapters/esp32/midi/picoTrackerMidiService.h"
#endif
#include "Application/Commands/NodeList.h"
#include "Application/Controllers/ControlRoom.h"
#include "Application/Model/Config.h"
#include "Application/Player/SyncMaster.h"
#include "System/Console/Logger.h"
#include "input.h"
#include <assert.h>
#include <fcntl.h>
#include <malloc.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "Adapters/esp32/platform/platform.h"
#include <stdlib.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

EventManager *picoTrackerSystem::eventManager_ = NULL;
bool picoTrackerSystem::invert_ = false;
int picoTrackerSystem::lastBattLevel_ = 100;
unsigned int picoTrackerSystem::lastBeatCount_ = 0;

adc_oneshot_unit_handle_t adc_handle;
adc_cali_handle_t adc_cali_handle;

int picoTrackerSystem::MainLoop() {

  eventManager_->InstallMappings();
  return eventManager_->MainLoop();
};

void picoTrackerSystem::Boot(int argc, char **argv) {

  // Install System
  static char systemMemBuf[sizeof(picoTrackerSystem)];
  System::Install(new (systemMemBuf) picoTrackerSystem());

  static char loggerMemBuf[sizeof(StdOutLogger)];
  Trace::GetInstance()->SetLogger(*(new (loggerMemBuf) StdOutLogger()));

  // Install GUI Factory
  static char guiMemBuf[sizeof(GUIFactory)];
  I_GUIWindowFactory::Install(new (guiMemBuf) GUIFactory());

  // Install Timers
  static char timerMemBuf[sizeof(picoTrackerTimerService)];
  TimerService::GetInstance()->Install(new (timerMemBuf)
                                           picoTrackerTimerService());

  // Install MIDI
  // **NOTE**: MIDI install MUST happen before Audio install because it triggers
  // reading config file and config file needs to have MidiService already
  // installed in order to apply midi settings read from the config file
#ifdef DUMMY_MIDI
  static char midiMemBuf[sizeof(DummyMidi)];
  MidiService::Install(new (midiMemBuf) DummyMidi());
#else
  static char midiMemBuf[sizeof(picoTrackerMidiService)];
  MidiService::Install(new (midiMemBuf) picoTrackerMidiService());
#endif

  // Install Sound
  AudioSettings hint;
  hint.bufferSize_ = 1024;
  hint.preBufferCount_ = 8;
  static char audioMemBuf[sizeof(picoTrackerAudio)];
  Audio::Install(new (audioMemBuf) picoTrackerAudio(hint));

  eventManager_ = I_GUIWindowFactory::GetInstance()->GetEventManager();
  eventManager_->Init();

  bool invert = false;
  Config *config = Config::GetInstance();
  invert = config->GetValue("INVERT") > 0;

  if (!invert) {
    eventManager_->MapAppButton("left ctrl", APP_BUTTON_A);
    eventManager_->MapAppButton("left alt", APP_BUTTON_B);
  } else {
    eventManager_->MapAppButton("left alt", APP_BUTTON_A);
    eventManager_->MapAppButton("left ctrl", APP_BUTTON_B);
  }
  eventManager_->MapAppButton("return", APP_BUTTON_START);
  //	em->MapElement("esc",APP_BUTTON_SELECT) ;
  eventManager_->MapAppButton("tab", APP_BUTTON_L);
  eventManager_->MapAppButton("backspace", APP_BUTTON_R);
  eventManager_->MapAppButton("right", APP_BUTTON_RIGHT);
  eventManager_->MapAppButton("left", APP_BUTTON_LEFT);
  eventManager_->MapAppButton("down", APP_BUTTON_DOWN);
  eventManager_->MapAppButton("up", APP_BUTTON_UP);

  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = ADC_UNIT_1,
    };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

  adc_oneshot_chan_cfg_t oneshot_config = {
            .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, BATT_VOLTAGE_ADC_CHANNEL, &oneshot_config));

  adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .chan = BATT_VOLTAGE_ADC_CHANNEL,
            .atten = ADC_ATTEN_DB_11,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
  ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));

  Trace::Log("PICOTRACKERSYSTEM", "ADC INIT DONE\n");
};

void picoTrackerSystem::Shutdown() { delete Audio::GetInstance(); };

static int secbase;

unsigned long picoTrackerSystem::GetClock() {
  struct timeval tp;

  gettimeofday(&tp, NULL);
  if (!secbase) {
    secbase = tp.tv_sec;
    return long(tp.tv_usec / 1000.0);
  }
  return long((tp.tv_sec - secbase) * 1000 + tp.tv_usec / 1000.0);
}

int picoTrackerSystem::GetBatteryLevel() {
  int adc_raw;
  int voltage;
  ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, BATT_VOLTAGE_ADC_CHANNEL, &adc_raw));
  ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage));

  voltage *= 2; // voltage divider reverse

  if (voltage >= 4200) {
        return 100;  // Cap at 100%
    } else if (voltage <= 3300) {
        return 0;    // Cap at 0%
    } else {
        // Linear interpolation
        return ((voltage - 3300) * 100 / (4200 - 3300));
    }
}

void picoTrackerSystem::Sleep(int millisec) {
  //	if (millisec>0)
  //		assert(0) ;
}

void *picoTrackerSystem::Malloc(unsigned size) {
  void *ptr = malloc(size);
  return ptr;
}

void picoTrackerSystem::Free(void *ptr) { free(ptr); }

void picoTrackerSystem::Memset(void *addr, char val, int size) {
  memset(addr, val, size);
};

void *picoTrackerSystem::Memcpy(void *s1, const void *s2, int n) {
  return memcpy(s1, s2, n);
}

void picoTrackerSystem::PostQuitMessage() { eventManager_->PostQuitMessage(); }

unsigned int picoTrackerSystem::GetMemoryUsage() {
  struct mallinfo m = mallinfo();
  return m.uordblks;
}
