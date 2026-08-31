#pragma once

#include <vector>

class Player {
public:
  struct NoteCall {
    unsigned short instrument;
    unsigned short voice;
    unsigned char note;
    unsigned char velocity;
  };

  struct StopCall {
    unsigned short instrument;
    unsigned short voice;
  };

  static Player *GetInstance() {
    static Player player;
    return &player;
  }

  static void ResetTestState() {
    auto *player = GetInstance();
    player->playedNotes.clear();
    player->stoppedNotes.clear();
    player->running = false;
    player->songStartCalls = 0;
  }

  void PlayNote(unsigned short instrument, unsigned short voice,
                unsigned char note, unsigned char velocity) {
    playedNotes.push_back({instrument, voice, note, velocity});
  }

  void StopNote(unsigned short instrument, unsigned short voice) {
    stoppedNotes.push_back({instrument, voice});
  }

  void OnSongStartButton(int, int, bool, bool) { ++songStartCalls; }
  bool IsRunning() const { return running; }
  void Stop() { running = false; }

  std::vector<NoteCall> playedNotes;
  std::vector<StopCall> stoppedNotes;
  bool running = false;
  int songStartCalls = 0;
};
