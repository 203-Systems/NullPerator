/* Deterministic host timing boundary for SampleInstrument command ramps. */
#pragma once

class SyncMaster {
public:
  static SyncMaster *GetInstance() {
    static SyncMaster instance;
    return &instance;
  }

  float GetTickSampleCount() const { return 100.0F; }
};
