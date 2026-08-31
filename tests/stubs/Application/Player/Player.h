/* Test-only player boundary used by MidiInstrument lifecycle coverage. */
#pragma once

class MidiInstrumentTestProject {
public:
  int GetScale() const { return 0; }
  int GetScaleRoot() const { return 0; }
};

class Player {
public:
  static Player *GetInstance() {
    static Player player;
    return &player;
  }

  MidiInstrumentTestProject *GetProject() { return &project_; }

private:
  MidiInstrumentTestProject project_{};
};
