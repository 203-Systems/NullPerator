/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include "Foundation/Types/Types.h"
#include <array>
#include <cstddef>
#include <string_view>

// Shared by the editor and renderer. These are the commands implemented by
// Player, TablePlayback and the instrument ProcessCommand handlers, not the
// larger set of features offered by the underlying synthesizers.
namespace fx {
enum class Instrument : uint8_t {
  Unknown,
  None,
  Sample,
  Midi,
  Sid,
  Opal,
  Drum,
  Stack,
  Chiptune
};

struct Context {
  Instrument instrument = Instrument::Unknown;
  bool table = false;
  bool operator==(const Context &) const = default;
};

constexpr uint16_t Bit(Instrument instrument) {
  return static_cast<uint16_t>(1U << static_cast<unsigned>(instrument));
}
inline constexpr auto sample = Bit(Instrument::Sample);
inline constexpr auto midi = Bit(Instrument::Midi);
inline constexpr auto sid = Bit(Instrument::Sid);
inline constexpr auto opal = Bit(Instrument::Opal);
inline constexpr auto drum = Bit(Instrument::Drum);
inline constexpr auto stack = Bit(Instrument::Stack);
inline constexpr auto chip = Bit(Instrument::Chiptune);

enum Page : uint8_t { Neither = 0, Phrase = 1, Table = 2, Both = 3 };
enum class Group : uint8_t { Standard, Sample, Midi, Synth };
inline constexpr std::array groups{Group::Standard, Group::Sample, Group::Midi,
                                   Group::Synth};
constexpr std::string_view GroupName(Group group) {
  constexpr std::array names{"Standard", "Sample", "MIDI", "Synth"};
  return names[static_cast<unsigned>(group)];
}
struct Command {
  FourCC::enum_type id;
  std::string_view name;
  uint16_t instruments = 0;
  Page common = Neither;
  Group group = Group::Standard;
};

// Keep the registry alphabetical for legacy command cycling. The selector
// groups these entries without changing command IDs or project data.
inline constexpr std::array<Command, 32> commands{{
    {FourCC::InstrumentCommandNone, "---", 0, Both},
    {FourCC::InstrumentCommandArpeggiator, "ARP", sample | stack | chip},
    {FourCC::InstrumentCommandChordBidirectional, "CHB", stack, Neither,
     Group::Synth},
    {FourCC::InstrumentCommandChordDown, "CHD", stack, Neither, Group::Synth},
    {FourCC::InstrumentCommandChordUp, "CHU", stack, Neither, Group::Synth},
    {FourCC::InstrumentCommandCrush, "CSH", sample | drum | stack | chip},
    {FourCC::InstrumentCommandDelay, "DLY", 0, Phrase},
    {FourCC::InstrumentCommandFilterCut, "FCT", sample, Neither, Group::Sample},
    {FourCC::InstrumentCommandLowPassFilter, "FLT", sample, Neither,
     Group::Sample},
    {FourCC::InstrumentCommandFilterResonance, "FRS", sample, Neither,
     Group::Sample},
    {FourCC::InstrumentCommandGateOff, "GOF", sid | opal | drum | stack | chip},
    {FourCC::InstrumentCommandGroove, "GRV", 0, Both},
    {FourCC::InstrumentCommandHop, "HOP", 0, Both},
    {FourCC::InstrumentCommandInstrumentRetrigger, "IRT", 0, Table},
    {FourCC::InstrumentCommandKill, "KIL", 0, Both},
    {FourCC::InstrumentCommandLegato, "LEG", sample | midi | stack | chip},
    {FourCC::InstrumentCommandLoopOfset, "LOF", sample, Neither, Group::Sample},
    {FourCC::InstrumentCommandMidiCC, "MCC", midi, Neither, Group::Midi},
    {FourCC::InstrumentCommandMidiChord, "MCH", midi, Neither, Group::Midi},
    {FourCC::InstrumentCommandMidiPC, "MPC", midi, Neither, Group::Midi},
    {FourCC::InstrumentCommandPan, "PAN", sample | stack | chip},
    {FourCC::InstrumentCommandPitchFineTune, "PFT", sample | stack | chip},
    {FourCC::InstrumentCommandPlayOfset, "POF", sample, Neither, Group::Sample},
    {FourCC::InstrumentCommandPitchSlide, "PSL", sample | midi | stack | chip},
    {FourCC::InstrumentCommandRetrigger, "RTG", sample | midi},
    {FourCC::InstrumentCommandSetInstrumentParameter, "SIP", stack | chip,
     Neither, Group::Synth},
    {FourCC::InstrumentCommandStop, "STP", 0, Table},
    {FourCC::InstrumentCommandTable, "TBL", 0, Phrase},
    {FourCC::InstrumentCommandTempo, "TPO", 0, Phrase},
    {FourCC::InstrumentCommandVelocity, "VEL", midi, Neither, Group::Midi},
    {FourCC::InstrumentCommandVibrato, "VIB", sample | stack | chip},
    {FourCC::InstrumentCommandVolume, "VOL",
     sample | midi | drum | stack | chip},
}};

constexpr const Command *Find(std::string_view name) {
  for (const auto &command : commands)
    if (command.name == name)
      return &command;
  return nullptr;
}
constexpr bool Available(const Command &command, Context context) {
  if (command.common != Neither)
    return (command.common & (context.table ? Table : Phrase)) != 0;
  return context.instrument == Instrument::Unknown ||
         (command.instruments & Bit(context.instrument)) != 0;
}
inline bool Available(FourCC id, Context context) {
  for (const auto &command : commands)
    if (command.id == id)
      return Available(command, context);
  return false;
}
constexpr std::string_view InstrumentName(Instrument instrument) {
  constexpr std::array names{"--",   "NONE", "SAMPLE", "MIDI",    "SID",
                             "OPAL", "DRUM", "STACK",  "CHIPTUNE"};
  return names[static_cast<unsigned>(instrument)];
}

// Parameter meanings are per engine even when the command mnemonic is shared.
struct ParameterInfo {
  std::string_view name;
  std::string_view range;
};
inline constexpr std::array<ParameterInfo, 10> stackParameters{
    {{"Wave", "00-06"},
     {"Transpose", "Signed semitones"},
     {"Volume", "00-FF"},
     {"Attack", "00-FF"},
     {"Decay", "00-FF"},
     {"Sustain", "00-FF"},
     {"Release", "00-FF"},
     {"Spread", "00-FF"},
     {"Brightness", "00-0C"},
     {"Pitch Decay", "00-FF"}}};
inline constexpr std::array<ParameterInfo, 12> chipParameters{
    {{"Wave", "00-07"},
     {"Transpose", "Signed semitones"},
     {"Volume", "00-FF"},
     {"Noise Burst", "00-FF"},
     {"Arp Speed", "00-22"},
     {"Length", "00 = unlimited"},
     {"Attack", "00-FF"},
     {"Decay", "00-FF"},
     {"Vibrato Delay", "00-FF"},
     {"Vibrato Depth", "00-FF"},
     {"Sweep Time", "00-FF"},
     {"Sweep Amount", "Signed byte"}}};
constexpr const ParameterInfo *InstrumentParameter(Instrument instrument,
                                                   uint8_t index) {
  if (instrument == Instrument::Stack && index < stackParameters.size())
    return &stackParameters[index];
  if (instrument == Instrument::Chiptune && index < chipParameters.size())
    return &chipParameters[index];
  return nullptr;
}

struct List {
  std::array<uint8_t, commands.size()> indices{};
  std::array<uint8_t, groups.size() + 1> starts{};
  std::size_t count = 0;
  static constexpr int rows = 6;
  constexpr const Command &operator[](std::size_t i) const {
    return commands[indices[i]];
  }
  constexpr int Begin(Group group) const {
    return starts[static_cast<unsigned>(group)];
  }
  constexpr int End(Group group) const {
    return starts[static_cast<unsigned>(group) + 1];
  }
  constexpr int GroupColumns(Group group) const {
    return (End(group) - Begin(group) + rows - 1) / rows;
  }
  constexpr int FirstColumn(Group group) const {
    int result = 0;
    for (const auto previous : groups) {
      if (previous == group)
        break;
      result += GroupColumns(previous);
    }
    return result;
  }
  constexpr int Columns() const {
    return FirstColumn(Group::Synth) + GroupColumns(Group::Synth);
  }
  constexpr int Column(std::size_t i) const {
    const auto group = (*this)[i].group;
    return FirstColumn(group) + (i - Begin(group)) / rows;
  }
  constexpr int Row(std::size_t i) const {
    return (i - Begin((*this)[i].group)) % rows;
  }
};
constexpr List BuildList(Context context, std::string_view original = {}) {
  List result;
  for (const auto group : groups) {
    result.starts[static_cast<unsigned>(group)] = result.count;
    for (std::size_t i = 0; i < commands.size(); ++i) {
      const auto &command = commands[i];
      // Page-specific flow commands remain filtered. A legacy command from the
      // other page is retained in its own group, never prepended or duplicated.
      if (command.group == group &&
          (command.common == Neither || Available(command, context) ||
           command.name == original))
        result.indices[result.count++] = static_cast<uint8_t>(i);
    }
  }
  result.starts[groups.size()] = result.count;
  return result;
}
inline FourCC Move(FourCC current, int dx, int dy, Context context,
                   std::string_view original = {}) {
  const auto list =
      BuildList(context, original.empty() ? current.c_str() : original);
  int index = 0;
  for (std::size_t i = 0; i < list.count; ++i)
    if (list[i].id == current)
      index = static_cast<int>(i);
  const int col = list.Column(index) + dx, row = list.Row(index) + dy;
  if (col < 0 || col >= list.Columns() || row < 0 || row >= List::rows)
    return current;
  // Horizontal moves into a short column land on its final entry. Vertical
  // moves stop at the column boundary; neither direction wraps.
  int next = -1;
  for (std::size_t i = 0; i < list.count; ++i) {
    if (list.Column(i) != col)
      continue;
    if (list.Row(i) == row)
      return list[i].id;
    if (dx != 0 && list.Row(i) < row)
      next = static_cast<int>(i);
  }
  return next >= 0 ? list[next].id : current;
}
} // namespace fx
