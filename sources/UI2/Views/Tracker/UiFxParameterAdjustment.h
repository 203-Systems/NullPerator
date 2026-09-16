/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Foundation/Types/FxCommands.h"
#include "UI2/Chrome/UiChromeModel.h"
#include "UI2/Text/UiFont5x7.h"
#include <algorithm>
#include <array>
#include <cstdio>

namespace ui2 {

// Owns formatted field text while the view builds its bottom-bar scene. The
// other strings refer to the frame's cell text or static command metadata.
class UiFxParameterAdjustment {
public:
  UiFxParameterAdjustment(std::string_view command, std::string_view value,
                          fx::Context context, std::uint8_t digit)
      : command_(command), value_(value), digit_(std::min<uint8_t>(digit, 3U)) {
    if (command.empty() || command == "---" || value.size() != 4U)
      return;
    const auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
      if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
      return -1;
    };
    for (char c : value) {
      if (nibble(c) < 0)
        return;
      raw_ = static_cast<std::uint16_t>((raw_ << 4) | nibble(c));
    }
    model.context = &context_;
    const auto *entry = fx::Find(command);
    if (entry == nullptr) {
      Single("Unknown FX", 0, 4, "Raw value");
      return;
    }
    if (!fx::Available(*entry, context)) {
      Notice("Not Supported", "On This Instrument");
      return;
    }
    const unsigned hi = raw_ >> 8, lo = raw_ & 255;
    const int signedLo = lo >= 128 ? int(lo) - 256 : int(lo);
    const bool midi = context.instrument == fx::Instrument::Midi;
    const bool synth = context.instrument == fx::Instrument::Stack ||
                       context.instrument == fx::Instrument::Chiptune;
    const bool unknown = context.instrument == fx::Instrument::Unknown;
    switch (entry->id) {
    case FourCC::InstrumentCommandSetInstrumentParameter:
      Sip(context);
      break;
    case FourCC::InstrumentCommandArpeggiator:
    case FourCC::InstrumentCommandMidiChord:
    case FourCC::InstrumentCommandChordUp:
    case FourCC::InstrumentCommandChordDown:
    case FourCC::InstrumentCommandChordBidirectional:
      Intervals(entry->id);
      break;
    case FourCC::InstrumentCommandVolume:
      if (midi || context.instrument == fx::Instrument::Drum ||
          context.instrument == fx::Instrument::Stack)
        Single("Volume", 2, 2, midi ? Format(4, "CC 7 = %u", lo / 2) : "00-FF");
      else
        Pair({"Time", 0, 2,
              unknown   ? "Per engine"
              : hi == 0 ? "Instant"
              : context.instrument == fx::Instrument::Chiptune
                  ? Format(4, "%u ms", hi * 10)
                  : Format(4, "%u ticks", hi * 4)},
             {"Volume", 2, 2, "00-FF"});
      break;
    case FourCC::InstrumentCommandMidiCC:
      Pair({"CC", 0, 2, Format(4, "MIDI %u", hi & 127)},
           {"Value", 2, 2, Format(5, "MIDI %u", lo & 127)});
      break;
    case FourCC::InstrumentCommandMidiPC:
      Single("Program", 2, 2, Format(4, "MIDI %u", lo & 127));
      break;
    case FourCC::InstrumentCommandVelocity:
      Single("Velocity", 2, 2, Format(4, "MIDI %u", lo & 127));
      break;
    case FourCC::InstrumentCommandVibrato:
      Pair({"Rate", 0, 2, hi == 0 ? "Off" : "00-FF"},
           {"Depth", 2, 2, lo == 0 ? "Off" : "00-FF"});
      break;
    case FourCC::InstrumentCommandFilterCut:
      Pair({"Time", 0, 2, hi == 0 ? "Instant" : Format(4, "%u ticks", hi * 4)},
           {"Cutoff", 2, 2, "00-FF"});
      break;
    case FourCC::InstrumentCommandFilterResonance:
      Pair({"Time", 0, 2, hi == 0 ? "Instant" : Format(4, "%u ticks", hi * 4)},
           {"Resonance", 2, 2, "00-FF"});
      break;
    case FourCC::InstrumentCommandLowPassFilter:
      Pair({"Cutoff", 0, 2, "00-FF"}, {"Resonance", 2, 2, "00-FF"});
      break;
    case FourCC::InstrumentCommandPan:
      Pair({synth     ? "Step"
            : unknown ? "Timing"
                      : "Time",
            0, 2,
            unknown   ? "Per engine"
            : hi == 0 ? "Instant"
            : synth   ? "per 10ms"
                      : Format(4, "%u ticks", hi * 4)},
           {"Pan", 2, 2,
            lo == 0                       ? "Right"
            : unknown                     ? "Per engine"
            : lo == (synth ? 128U : 127U) ? "Center"
            : lo >= (synth ? 255U : 254U) ? "Left"
                                          : "00-FF"});
      break;
    case FourCC::InstrumentCommandPitchSlide:
    case FourCC::InstrumentCommandLegato:
      Pair({synth ? "Time" : "Speed", 0, 2,
            hi == 0 ? "Instant"
            : synth ? Format(5, "%u ms", hi * 10)
                    : "00-FF"},
           {midi ? "Bend" : "Pitch", 2, 2,
            unknown ? "Per engine"
            : midi  ? "00-FF"
            : entry->id == FourCC::InstrumentCommandLegato && lo == 0
                ? "Prev note"
                : Format(4, "%+d st", signedLo)});
      break;
    case FourCC::InstrumentCommandPitchFineTune: {
      // Sample historically treats 80 as +1; the synth engines use -1.
      const int amount = !synth && !unknown && lo == 128 ? 128 : signedLo;
      Pair({synth ? "Time" : "Speed", 0, 2,
            hi == 0 ? "Instant"
            : synth ? Format(5, "%u ms", hi * 10)
                    : "00-FF"},
           {"Tune", 2, 2,
            unknown ? "Per engine" : Format(4, "%+d/128 st", amount)});
      break;
    }
    case FourCC::InstrumentCommandCrush:
      if (synth || context.instrument == fx::Instrument::Drum)
        Single("Crush", 3, 1, "0-F");
      else
        Pair({"Drive", 0, 2,
              unknown   ? "Per engine"
              : hi == 0 ? "Keep"
                        : "00-FF"},
             {"Crush", 3, 1, !unknown && (lo & 15) == 0 ? "Keep" : "0-F"});
      break;
    case FourCC::InstrumentCommandRetrigger:
      if (midi)
        Single("Retrigger", 2, 2, lo == 0 ? "Off" : Format(4, "%u ticks", lo));
      else
        Pair({"Offset", 0, 2,
              unknown ? "Per engine" : Format(5, "%u ticks", hi)},
             {"Repeat", 2, 2, lo == 0 ? "Off" : Format(4, "%u ticks", lo)});
      break;
    case FourCC::InstrumentCommandPlayOfset:
      Pair({"Absolute", 0, 2, hi == 0 ? "Keep" : Format(4, "%u/256", hi)},
           {"Relative", 2, 2, Format(5, "%+d/256", signedLo)});
      break;
    case FourCC::InstrumentCommandLoopOfset:
      Single("Loop offset", 0, 4,
             Format(4, "%+d samples",
                    raw_ > 0x8000 ? int(raw_) - 65536 : int(raw_)));
      break;
    case FourCC::InstrumentCommandTempo:
      Single("Tempo", 0, 4,
             Format(4, "%u BPM", std::clamp<unsigned>(raw_, 60, 400)));
      break;
    case FourCC::InstrumentCommandDelay:
      Single("Note delay", 3, 1, Format(4, "%u ticks", lo & 15));
      break;
    case FourCC::InstrumentCommandKill:
      Single("Stop after", 2, 2, Format(4, "%u ticks", lo));
      break;
    case FourCC::InstrumentCommandInstrumentRetrigger:
      Single("Retrigger", 2, 2, Format(4, "%+d st", signedLo));
      break;
    case FourCC::InstrumentCommandHop:
      if (context.table)
        Pair({"Repeat", 0, 2, hi == 0 ? "Always" : Format(4, "%u times", hi)},
             {"Step", 3, 1, "0-F"});
      else
        Single("Jump to step", 3, 1, "0-F");
      break;
    case FourCC::InstrumentCommandGroove:
      if (context.table)
        Single("Groove", 2, 2, Format(4, "Groove %02X", lo & 31));
      else
        Pair({"Scope", 0, 2, hi == 0 ? "Track" : "All tracks"},
             {"Groove", 2, 2, lo < 32 ? "00-1F" : "Ignored"});
      break;
    case FourCC::InstrumentCommandTable:
      Single("Run table", 2, 2,
             (lo & 127) < 32 ? Format(4, "Table %02X", lo & 127) : "Ignored");
      break;
    case FourCC::InstrumentCommandGateOff:
      Action("Gate off");
      break;
    case FourCC::InstrumentCommandStop:
      Action("Stop table");
      break;
    default:
      Single("Parameter", 0, 4, "Raw value");
      break;
    }
  }

  UiFxParameterAdjustment(const UiFxParameterAdjustment &) = delete;
  UiFxParameterAdjustment &operator=(const UiFxParameterAdjustment &) = delete;

  UiAdjustmentLegendModel model{.fineLabel = "DIGIT", .coarseLabel = "VALUE"};

private:
  struct Field {
    std::string_view name;
    std::uint8_t start;
    std::uint8_t count;
    std::string_view detail;
    bool Contains(std::uint8_t digit) const {
      return digit >= start && digit < start + count;
    }
  };

  template <typename... Args>
  std::string_view Format(std::size_t index, const char *format, Args... args) {
    std::snprintf(text_[index].data(), text_[index].size(), format, args...);
    return text_[index].data();
  }

  void Notice(std::string_view first, std::string_view second) {
    context_.firstLineCount = context_.secondLineCount = 1;
    context_.firstLine[0] = {first, UiColorToken::TextColored};
    context_.secondLine[0] = {second, UiColorToken::TextNormal};
  }

  void Title(std::string_view title) {
    context_.firstLineCount = 2;
    context_.firstLine[0] = {command_, UiColorToken::TextDim};
    context_.firstLine[1] = {title, UiColorToken::TextNormal};
  }

  void Action(std::string_view title) {
    Title(title);
    context_.secondLineCount = 1;
    context_.secondLine[0] = {"No parameter", UiColorToken::TextDim};
  }

  void Single(std::string_view title, std::uint8_t start, std::uint8_t count,
              std::string_view detail) {
    Title(title);
    const bool active = digit_ >= start && digit_ < start + count;
    // Keep the actual parameter value visible even on an unused digit. Showing
    // the focused nibble here would mislabel it as the jump/volume/etc. value.
    if (!active)
      detail = "Unused digit";
    context_.secondLineCount = 2;
    context_.secondLine[0] = {value_.substr(start, count),
                              active ? UiColorToken::TextColored
                                     : UiColorToken::TextNormal};
    context_.secondLine[1] = {
        Format(3, "/ %.*s", int(detail.size()), detail.data()),
        UiColorToken::TextDim};
  }

  void Pair(Field hi, Field lo) {
    if (!hi.Contains(digit_) && !lo.Contains(digit_))
      lo.detail = "Unused digit";
    const auto fieldText = [&](std::size_t index, const Field &field) {
      return Format(index, "%.*s %.*s", int(field.name.size()),
                    field.name.data(), int(field.count),
                    value_.data() + field.start);
    };
    context_.firstLineCount = 3;
    context_.firstLine[0] = {command_, UiColorToken::TextDim};
    context_.firstLine[1] = {fieldText(0, hi), hi.Contains(digit_)
                                                   ? UiColorToken::TextColored
                                                   : UiColorToken::TextNormal};
    context_.firstLine[2] = {
        Format(1, "/ %.*s", int(hi.detail.size()), hi.detail.data()),
        UiColorToken::TextDim};
    context_.secondLineCount = 2;
    context_.secondLine[0] = {fieldText(2, lo), lo.Contains(digit_)
                                                    ? UiColorToken::TextColored
                                                    : UiColorToken::TextNormal};
    context_.secondLine[1] = {
        Format(3, "/ %.*s", int(lo.detail.size()), lo.detail.data()),
        UiColorToken::TextDim};
  }

  void Intervals(FourCC::enum_type command) {
    Title(command == FourCC::InstrumentCommandMidiChord ? "Scale offsets"
                                                        : "Semitones");
    std::array<char, 24> offsets{};
    std::size_t used = 0, selectedStart = 0, selectedLength = 0;
    for (std::uint8_t i = 0; i < 4; ++i) {
      const int shift = (3 - i) * 4;
      int offset = (raw_ >> shift) & 15;
      const bool absent =
          command == FourCC::InstrumentCommandMidiChord
              ? offset == 0
              : command == FourCC::InstrumentCommandArpeggiator &&
                    (raw_ & ((1U << (shift + 4)) - 1)) == 0;
      if (command == FourCC::InstrumentCommandChordDown)
        offset = -offset;
      else if (command == FourCC::InstrumentCommandChordBidirectional &&
               offset >= 8)
        offset -= 16;
      if (i != 0)
        offsets[used++] = ' ';
      std::size_t length = 1;
      if (absent)
        offsets[used] = '-';
      else
        length = static_cast<std::size_t>(std::snprintf(
            offsets.data() + used, offsets.size() - used, "%+d", offset));
      if (i == digit_) {
        selectedStart = used;
        selectedLength = length;
      }
      used += length;
    }
    text_[0] = {};
    std::copy_n(offsets.data(), used, text_[0].data());
    const std::string_view line(text_[0].data(), used);
    context_.secondLineCount = 3;
    context_.secondLine[0] = {line.substr(0, selectedStart),
                              UiColorToken::TextNormal, 9};
    context_.secondLine[1] = {
        line.substr(selectedStart, selectedLength), UiColorToken::TextColored,
        static_cast<std::int16_t>(9 + selectedStart * UiFont5x7::kAdvance)};
    context_.secondLine[2] = {
        line.substr(selectedStart + selectedLength), UiColorToken::TextNormal,
        static_cast<std::int16_t>(9 + (selectedStart + selectedLength) *
                                          UiFont5x7::kAdvance)};
  }

  void Sip(fx::Context context) {
    const auto index = static_cast<std::uint8_t>(raw_ >> 8);
    const auto *parameter = fx::InstrumentParameter(context.instrument, index);
    const bool unresolved = context.instrument == fx::Instrument::Unknown;
    const bool supported =
        fx::Available(FourCC::InstrumentCommandSetInstrumentParameter, context);
    const std::string_view range = parameter != nullptr ? parameter->range
                                   : unresolved         ? "Instrument map"
                                   : supported          ? "Ignored"
                                                        : "Not supported";
    std::snprintf(text_[3].data(), text_[3].size(), "/ %.*s",
                  static_cast<int>(range.size()), range.data());

    const auto parameterColor =
        digit_ < 2U ? UiColorToken::TextColored : UiColorToken::TextNormal;
    const auto valueColor =
        digit_ >= 2U ? UiColorToken::TextColored : UiColorToken::TextNormal;
    auto &bar = context_;
    model.context = &bar;
    bar.firstLineCount = unresolved ? 2U : 3U;
    bar.firstLine[0] = {unresolved ? "Parameter"
                                   : fx::InstrumentName(context.instrument),
                        UiColorToken::TextDim};
    bar.firstLine[1] = {value_.substr(0, 2), parameterColor};
    bar.firstLine[2] = {parameter != nullptr ? parameter->name
                        : supported          ? "Unknown"
                                             : "Parameter",
                        parameterColor};
    if (unresolved)
      bar.firstLine[0].color = parameterColor;
    bar.secondLineCount = 3U;
    bar.secondLine[0] = {"VALUE", valueColor, 9};
    bar.secondLine[1] = {value_.substr(2, 2), valueColor, 45};
    bar.secondLine[2] = {text_[3].data(), UiColorToken::TextDim, 63};
  }

  std::array<std::array<char, 32>, 6> text_{};
  UiContextBarModel context_{};
  std::string_view command_;
  std::string_view value_;
  std::uint8_t digit_;
  std::uint16_t raw_ = 0;
};

} // namespace ui2
