#pragma once

#include "UI2/Views/Instrument/UiInstrumentView.h"

namespace ui2::test {

inline UiInstrumentViewData ApprovedInstrumentFixture(std::string_view state) {
  UiInstrumentViewData data;
  data.trackNotes = {"D3", "C4", "--", "F2", "A3", "D#3", "C3", "G2"};
  data.selectedTrack = 2;
  data.cursor = UiInstrumentCursor::Type;
  if (state == "name")
    data.cursor = UiInstrumentCursor::Name;
  if (state == "number") {
    data.cursor = UiInstrumentCursor::None;
    data.numberFocus = true;
  }

  if (state == "none") {
    data.kind = UiInstrumentKind::None;
    data.name = "--";
    return data;
  }
  if (state == "midi") {
    data.kind = UiInstrumentKind::Midi;
    data.name = "LEAD";
    data.fields = {{{"CHANNEL", "01", 66},
                    {"VOLUME", "FF", 76},
                    {"LENGTH", "00", 86},
                    {"PROGRAM", "--", 96},
                    {"AUTOMATION", "FALSE", 106},
                    {"TABLE", "--", 116}}};
    data.fieldCount = 6;
    return data;
  }
  if (state == "sid") {
    data.kind = UiInstrumentKind::Sid;
    data.name = "PULSE";
    data.fields = {{{"OSCILLATOR", "PULSEWIDTH 800", 66},
                    {"WAVEFORM", "A", 76},
                    {"OSC SYNC", "FALSE", 86},
                    {"RING MOD", "FALSE", 96},
                    {"ENV ADSR", "2282", 106},
                    {"FILTER", "CUTOFF 1FF", 116},
                    {"RESONANCE", "0", 126},
                    {"MODE", "LP", 136}}};
    data.fieldCount = 8;
    return data;
  }
  if (state == "opal") {
    data.kind = UiInstrumentKind::Opal;
    data.name = "E PIANO";
    data.fields = {{{"ALGORITHM", "1+2", 82},
                    {"DEEP TREM/VIB", "00", 93},
                    {"FEEDBACK", "0", 104}}};
    data.fieldCount = 3;
    data.operators = {{{"LEVEL", "17", "00"},
                       {"MULTIPLIER", "1", "1"},
                       {"A/D/S/R", "F1C8", "F108"},
                       {"SHAPE", "SINE", "SINE"},
                       {"TR/VB/SU/KSR", "0000", "0010"},
                       {"KEYSCALE", "1.5", "0"}}};
    data.operatorCount = 6;
    return data;
  }

  data.kind = UiInstrumentKind::Sample;
  data.name = "AKWF 0906";
  data.fields = {{{"SAMPLE", "AKWF 0906.WAV", 66},
                  {"SLICES", "OFF / ADJUST", 76},
                  {"VOLUME", "E5", 86},
                  {"PAN", "7F", 96},
                  {"ROOT NOTE", "C3", 106},
                  {"DETUNE", "7F", 116},
                  {"DRIVE", "FF", 126},
                  {"CRUSH", "16", 136},
                  {"DOWNSAMPLE", "0", 146},
                  {"FILTER", "LP / DF 1E", 156},
                  {"LOOP", "FORWARD", 166}}};
  data.fieldCount = 11;
  return data;
}

} // namespace ui2::test
