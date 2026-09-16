/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#pragma once

#include "Foundation/Types/FxCommands.h"
#include <array>

// Phrase flow and instrument handlers do not interpret every byte alike.
// Dashes mark unused digits. Keep both lines within the help bar's 31 columns.
inline std::array<const char *, 2> getHelpLegend(FourCC command,
                                                 fx::Context context = {}) {
  const bool midi = context.instrument == fx::Instrument::Midi;
  const bool sample = context.instrument == fx::Instrument::Sample;
  const bool chip = context.instrument == fx::Instrument::Chiptune;
  const bool stack = context.instrument == fx::Instrument::Stack;
  const bool drum = context.instrument == fx::Instrument::Drum;
  const bool synth = stack || chip;
  const bool unknown = context.instrument == fx::Instrument::Unknown;
  switch (command) {
  case FourCC::InstrumentCommandNone:
    return {"No command", "clear the FX command"};
  case FourCC::InstrumentCommandKill:
    return {"KILl: --bb", "stop playing after bb ticks"};
  case FourCC::InstrumentCommandLoopOfset:
    return {"Loop Offset: aaaa", "shift loop by aaaa samples"};
  case FourCC::InstrumentCommandArpeggiator:
    return {"Arpeggio: abcd", "base note + offsets a b c d"};
  case FourCC::InstrumentCommandVolume:
    if (midi)
      return {"Volume: --bb", "send MIDI CC 7 = bb/2"};
    if (drum || stack)
      return {"Volume: --bb", "set volume bb immediately"};
    if (chip)
      return {"Volume: aabb", "volume bb, time aa x 10ms"};
    return {"Volume: aabb", sample ? "volume bb, time aa x 4 ticks"
                                   : "volume bb; aa is per engine"};
  case FourCC::InstrumentCommandVelocity:
    return {"Velocity: --bb", "MIDI note velocity bb (7bit)"};
  case FourCC::InstrumentCommandPitchSlide:
    return {"Pitch Slide: aabb", midi    ? "speed aa, MIDI bend bb"
                                 : synth ? "pitch bb, time aa x 10ms"
                                         : "speed aa, signed pitch bb"};
  case FourCC::InstrumentCommandHop:
    return context.table
               ? std::array<const char *, 2>{"Hop: aa-b",
                                             "hop to b aa times (00=loop)"}
               : std::array<const char *, 2>{"Hop: ---b",
                                             "jump to step b; no repeat count"};
  case FourCC::InstrumentCommandLegato:
    return {"Legato: aabb", midi    ? "speed aa, curved MIDI bend bb"
                            : synth ? "pitch bb, time aa x 10ms"
                                    : "speed aa, pitch bb (00=prev)"};
  case FourCC::InstrumentCommandRetrigger:
    return {midi ? "Retrigger: --bb" : "Retrigger: aabb",
            midi      ? "repeat each bb ticks (00=off)"
            : unknown ? "repeat bb; aa is per engine"
                      : "repeat bb, offset aa (ticks)"};
  case FourCC::InstrumentCommandTempo:
    return {"Tempo: aaaa", "hex BPM, clamped to 60-400"};
  case FourCC::InstrumentCommandMidiCC:
    return {"MIDI CC: aabb", "CC number aa, value bb (7bit)"};
  case FourCC::InstrumentCommandMidiPC:
    return {"MIDI PC: --bb", "send program change bb (7bit)"};
  case FourCC::InstrumentCommandPlayOfset:
    return {"Play Offset: aabb", "abs aa, rel signed bb (/256)"};
  case FourCC::InstrumentCommandFilterResonance:
    return {"Resonance: aabb", "resonance bb, time aa x 4 ticks"};
  case FourCC::InstrumentCommandLowPassFilter:
    return {"Filter: aabb", "cutoff aa, resonance bb"};
  case FourCC::InstrumentCommandTable:
    return {"Table: --bb", "run table bb"};
  case FourCC::InstrumentCommandCrush:
    return {synth || drum ? "Crush: ---b" : "Drive & Crush: aa-b",
            synth || drum ? "set crush b (0=off)"
            : sample      ? "drive aa, crush b (0=keep)"
                          : "crush b; drive aa is per engine"};
  case FourCC::InstrumentCommandFilterCut:
    return {"Cutoff: aabb", "cutoff bb, time aa x 4 ticks"};
  case FourCC::InstrumentCommandPan:
    return {"Pan: aabb", sample  ? "pan bb, time aa x 4 ticks"
                         : synth ? "pan bb, step aa per 10ms"
                                 : "pan bb; aa is per engine"};
  case FourCC::InstrumentCommandGroove:
    return {context.table ? "Groove: --bb" : "Groove: aabb",
            context.table ? "table groove bb (low 5 bits)"
                          : "groove bb; aa>0 for all tracks"};
  case FourCC::InstrumentCommandInstrumentRetrigger:
    return {"Instrument Retrigger: --bb", "retrigger, signed transpose bb"};
  case FourCC::InstrumentCommandPitchFineTune:
    return {"Fine Tune: aabb",
            synth ? "tune bb, time aa x 10ms" : "speed aa, tune bb (~+/-1 st)"};
  case FourCC::InstrumentCommandDelay:
    return {"Delay: ---b", "delay note by b ticks"};
  case FourCC::InstrumentCommandStop:
    return {"Stop: ----", "stop table playback"};
  case FourCC::InstrumentCommandGateOff:
    return {"Gate Off: ----", drum || chip ? "stop the voice"
                              : unknown    ? "release or stop, per instrument"
                                           : "release the synth envelope"};
  case FourCC::InstrumentCommandSetInstrumentParameter:
    return {"SIP: aabb", "parameter aa, value bb"};
  case FourCC::InstrumentCommandChordUp:
    return {"Chord Up: abcd", "Stack offsets +a +b +c +d"};
  case FourCC::InstrumentCommandChordDown:
    return {"Chord Down: abcd", "Stack offsets -a -b -c -d"};
  case FourCC::InstrumentCommandChordBidirectional:
    return {"Chord Both: abcd", "signed offsets: 8..F = -8..-1"};
  case FourCC::InstrumentCommandVibrato:
    return {"Vibrato: aabb", "rate aa, depth bb (bb=00 off)"};
  case FourCC::InstrumentCommandMidiChord:
    return {"MIDI Chord: abcd", "scale offsets a b c d (0=skip)"};
  default:
    return {"", ""};
  }
}
