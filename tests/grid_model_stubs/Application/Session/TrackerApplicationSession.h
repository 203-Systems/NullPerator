/* Test-only storage model for the real UI2 tracker session adapter. */
#pragma once

#include "Foundation/Types/Types.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>

#define SONG_CHANNEL_COUNT 8
#define SONG_ROW_COUNT 128
#define CHAIN_COUNT 0xFF
#define NO_MORE_CHAIN 0x100
#define PHRASES_PER_CHAIN 0x10
#define PHRASE_COUNT 0xFF
#define NO_MORE_PHRASE 0x100
#define STEPS_PER_PHRASE 16
#define MAX_INSTRUMENT_COUNT 0x27
#define HIGHEST_NOTE 119
#define NOTE_OFF 0xFE
#define NO_NOTE 0xFF
#define NOTE_C3 60
#define NO_MORE_INSTRUMENT 0x100
#define VAR_OFF -1

enum InstrumentType { IT_NONE = 0, IT_SAMPLE, IT_MIDI, IT_SID, IT_OPAL, IT_LAST };

enum PlayMode { PM_SONG, PM_CHAIN, PM_PHRASE, PM_LIVE, PM_AUDITION };

class I_Instrument {
public:
  virtual ~I_Instrument() = default;
  virtual InstrumentType GetType() const { return type_; }
  virtual int GetTable() const { return table_; }
  void SetType(InstrumentType type) { type_ = type; }
  void SetTable(int table) { table_ = table; }

private:
  InstrumentType type_ = IT_NONE;
  int table_ = VAR_OFF;
};

class SampleInstrument : public I_Instrument {
public:
  SampleInstrument() { SetType(IT_NONE); }

  bool GetSliceNoteRange(std::uint8_t &first, std::uint8_t &last) const {
    if (!sliceRangeActive_)
      return false;
    first = sliceFirst_;
    last = sliceLast_;
    return true;
  }

  void SetSliceNoteRange(std::uint8_t first, std::uint8_t last) {
    sliceRangeActive_ = true;
    sliceFirst_ = first;
    sliceLast_ = last;
    SetType(IT_SAMPLE);
  }

private:
  bool sliceRangeActive_ = false;
  std::uint8_t sliceFirst_ = 0U;
  std::uint8_t sliceLast_ = 0U;
};

class InstrumentBank {
public:
  InstrumentBank() {
    for (std::size_t index = 0; index < instruments_.size(); ++index)
      instruments_[index].SetTable(VAR_OFF);
  }

  unsigned short GetNextFreeInstrumentSlotId() const {
    for (std::size_t index = 0; index < used_.size(); ++index) {
      if (!used_[index])
        return static_cast<unsigned short>(index);
    }
    return NO_MORE_INSTRUMENT;
  }

  unsigned short GetNextAndAssignID(InstrumentType type, unsigned char id) {
    if (id >= instruments_.size() || used_[id])
      return NO_MORE_INSTRUMENT;
    used_[id] = true;
    instruments_[id].SetType(type);
    return id;
  }

  I_Instrument *GetInstrument(int id) {
    return id >= 0 && id < static_cast<int>(instruments_.size()) && used_[id]
               ? &instruments_[id]
               : nullptr;
  }

  void SetInstrumentTable(std::uint8_t id, int table) {
    if (!used_[id])
      (void)GetNextAndAssignID(IT_NONE, id);
    instruments_[id].SetTable(table);
  }

  void SetSampleSliceRange(std::uint8_t id, std::uint8_t first,
                           std::uint8_t last) {
    if (!used_[id]) {
      used_[id] = true;
      instruments_[id].SetType(IT_SAMPLE);
    }
    instruments_[id].SetSliceNoteRange(first, last);
  }

private:
  std::array<SampleInstrument, MAX_INSTRUMENT_COUNT> instruments_{};
  std::array<bool, MAX_INSTRUMENT_COUNT> used_{};
};

class Chain {
public:
  Chain() { Reset(); }

  void Reset() {
    std::fill(std::begin(data_), std::end(data_), 0xFFU);
    std::fill(std::begin(transpose_), std::end(transpose_), 0U);
    used_.reset();
  }

  unsigned short GetNext() {
    for (unsigned index = 0; index < CHAIN_COUNT; ++index) {
      if (!used_[index]) {
        used_[index] = true;
        return static_cast<unsigned short>(index);
      }
    }
    return NO_MORE_CHAIN;
  }

  bool IsUsed(unsigned char index) const { return used_[index]; }
  void SetUsed(unsigned char index) { used_[index] = true; }
  void ClearAllocation() { used_.reset(); }

  unsigned char data_[CHAIN_COUNT * PHRASES_PER_CHAIN]{};
  unsigned char transpose_[CHAIN_COUNT * PHRASES_PER_CHAIN]{};

private:
  std::bitset<CHAIN_COUNT> used_{};
};

class Phrase {
public:
  Phrase() { Reset(); }

  void Reset() {
    std::fill(std::begin(note_), std::end(note_), NO_NOTE);
    std::fill(std::begin(instr_), std::end(instr_), 0xFFU);
    std::fill(std::begin(cmd1_), std::end(cmd1_),
              FourCC::InstrumentCommandNone);
    std::fill(std::begin(param1_), std::end(param1_), 0U);
    std::fill(std::begin(cmd2_), std::end(cmd2_),
              FourCC::InstrumentCommandNone);
    std::fill(std::begin(param2_), std::end(param2_), 0U);
    used_.fill(false);
  }

  unsigned short GetNext() {
    for (unsigned index = 0; index < PHRASE_COUNT; ++index) {
      if (!used_[index]) {
        used_[index] = true;
        return static_cast<unsigned short>(index);
      }
    }
    return NO_MORE_PHRASE;
  }

  bool IsUsed(unsigned char index) const {
    return index < PHRASE_COUNT && used_[index];
  }
  void SetUsed(unsigned char index) {
    if (index < PHRASE_COUNT)
      used_[index] = true;
  }
  void ClearAllocation() { used_.fill(false); }

  unsigned char note_[PHRASE_COUNT * STEPS_PER_PHRASE]{};
  unsigned char instr_[PHRASE_COUNT * STEPS_PER_PHRASE]{};
  FourCC cmd1_[PHRASE_COUNT * STEPS_PER_PHRASE]{};
  unsigned short param1_[PHRASE_COUNT * STEPS_PER_PHRASE]{};
  FourCC cmd2_[PHRASE_COUNT * STEPS_PER_PHRASE]{};
  unsigned short param2_[PHRASE_COUNT * STEPS_PER_PHRASE]{};

private:
  std::array<bool, PHRASE_COUNT> used_{};
};

class Song {
public:
  Song() { std::fill(std::begin(data_), std::end(data_), 0xFFU); }

  unsigned char data_[SONG_CHANNEL_COUNT * SONG_ROW_COUNT]{};
  Chain chain_{};
  Phrase phrase_{};
};

class Project {
public:
  void NudgeTempo(int value) { tempoNudge_ += value; }
  InstrumentBank *GetInstrumentBank() { return &instrumentBank_; }
  const InstrumentBank *GetInstrumentBank() const { return &instrumentBank_; }
  int GetScale() const { return scale_; }
  std::uint8_t GetScaleRoot() const { return scaleRoot_; }
  void SetScale(int scale, std::uint8_t root) {
    scale_ = scale;
    scaleRoot_ = root;
  }

  Song song_{};
  int tempoNudge_ = 0;

private:
  InstrumentBank instrumentBank_{};
  int scale_ = 0;
  std::uint8_t scaleRoot_ = 0U;
};

class TrackerSessionState {
public:
  int songX_ = 0;
  int songY_ = 0;
  int songOffset_ = 0;
  int chainRow_ = 0;
  int chainCol_ = 0;
  int currentChain_ = 0;
  int currentPhrase_ = 0;
  int currentInstrumentID_ = 0;
  int currentTable_ = 0;
  int phraseCurPos_ = 0;
  PlayMode playMode_ = PM_SONG;
};

class TrackerApplicationSession {
public:
  [[nodiscard]] Project &ProjectModel() { return project_; }
  [[nodiscard]] const Project &ProjectModel() const { return project_; }
  [[nodiscard]] TrackerSessionState &EditorState() { return editor_; }
  [[nodiscard]] const TrackerSessionState &EditorState() const {
    return editor_;
  }

private:
  Project project_{};
  TrackerSessionState editor_{};
};
