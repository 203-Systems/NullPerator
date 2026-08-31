/* Deterministic host audio boundary for instrument lifecycle coverage. */
#pragma once

class Audio {
public:
  static Audio *GetInstance() {
    static Audio instance;
    return &instance;
  }

  int GetSampleRate() const { return 44100; }
};
