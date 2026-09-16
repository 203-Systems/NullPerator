#include "Application/Instruments/GBInstrument.h"
#include "Application/UI2/Ui2InstrumentParameters.h"
#include "Foundation/Types/FxCommands.h"
#include "doctest/doctest.h"
#include <algorithm>
#include <array>

TEST_CASE_TEMPLATE("GB presets expose exactly their editable persisted fields",
                   Synth, GBWaveInstrument, GBPulseInstrument,
                   GBNoiseInstrument) {
  Synth synth;
  CHECK(synth.Init());
  CHECK(synth.Variables()->size() ==
        ui2::Ui2InstrumentFieldCount(synth.GetType()));
  for (unsigned i = 0; i < ui2::Ui2InstrumentFieldCount(synth.GetType()); ++i) {
    auto descriptor = ui2::Ui2InstrumentFieldParameter(synth.GetType(), i);
    REQUIRE(descriptor.Valid());
    REQUIRE(synth.FindVariable(descriptor.primary));
  }
  CHECK(synth.GetTable() == VAR_OFF);
  CHECK_FALSE(synth.GetTableAutomation());
}

TEST_CASE_TEMPLATE("GB native volume pan and stop FX have audible effects",
                   Synth, GBWaveInstrument, GBPulseInstrument,
                   GBNoiseInstrument) {
  Synth synth;
  std::array<fixed, 2048> buffer{};
  REQUIRE(synth.Start(0, 69));
  synth.ProcessCommand(0, FourCC::InstrumentCommandPan, 0x00FF);
  REQUIRE(synth.Render(0, buffer.data(), 1024, false));
  bool left = false;
  for (int i = 0; i < 1024; ++i) {
    left |= buffer[i * 2] != 0;
    CHECK(buffer[i * 2 + 1] == 0);
  }
  CHECK(left);
  synth.ProcessCommand(0, FourCC::InstrumentCommandVolume, 0);
  REQUIRE(synth.Render(0, buffer.data(), 1024, false));
  CHECK(std::all_of(buffer.begin(), buffer.end(),
                    [](fixed x) { return x == 0; }));
  synth.ProcessCommand(0, FourCC::InstrumentCommandGateOff, 0);
  CHECK_FALSE(synth.Render(0, buffer.data(), 1024, false));
}

TEST_CASE("GB modes share a track pool without retired preset interference") {
  TrackVoicePool<gb::Voice> pool;
  GBPulseInstrument pulse(&pool);
  GBWaveInstrument wave(&pool);
  GBNoiseInstrument noise(&pool);
  std::array<fixed, 512> buffer{};
  REQUIRE(pulse.Start(0, 60));
  REQUIRE(wave.Start(0, 64));
  pulse.Stop(0);
  pulse.ProcessCommand(0, FourCC::InstrumentCommandKill, 0);
  CHECK_FALSE(pulse.Render(0, buffer.data(), 256, false));
  CHECK(wave.Render(0, buffer.data(), 256, false));
  REQUIRE(noise.Start(1, 60));
  wave.Stop(0);
  CHECK(noise.Render(1, buffer.data(), 256, false));
}

TEST_CASE(
    "GB Wave exposes 32 editable samples and SIP changes only the voice") {
  TrackVoicePool<gb::Voice> pool;
  GBWaveInstrument synth(&pool);
  synth.FindVariable(FourCC::GBWave0)->SetInt(0x1234);
  synth.FindVariable(FourCC::GBWave7)->SetInt(0xABCD);
  REQUIRE(synth.Start(0, 60));
  CHECK(pool.voices[0].wave[0] == 0x12);
  CHECK(pool.voices[0].wave[1] == 0x34);
  CHECK(pool.voices[0].wave[15] == 0xCD);
  synth.ProcessCommand(0, FourCC::InstrumentCommandSetInstrumentParameter,
                       0x100F);
  synth.ProcessCommand(0, FourCC::InstrumentCommandSetInstrumentParameter,
                       0x2F00);
  CHECK(pool.voices[0].wave[0] == 0xF2);
  CHECK(pool.voices[0].wave[15] == 0xC0);
  CHECK(synth.FindVariable(FourCC::GBWave0)->GetInt() == 0x1234);
  REQUIRE(synth.Start(1, 60));
  CHECK(pool.voices[1].wave[0] == 0x12);
  synth.ProcessCommand(0, FourCC::InstrumentCommandSetInstrumentParameter,
                       0x0000);
  std::array<fixed, 512> buffer{};
  REQUIRE(synth.Render(0, buffer.data(), 256, false));
  CHECK(std::all_of(buffer.begin(), buffer.end(),
                    [](fixed x) { return x == 0; }));
}

TEST_CASE("GB pulse has four exact duty patterns and frequency quantization") {
  gb::Voice voice;
  voice.envelopeVolume = 15;
  for (int duty = 0; duty < 4; ++duty) {
    voice.duty = duty;
    int high = 0;
    for (int i = 0; i < 8; ++i) {
      voice.position = i;
      high += voice.digitalSample() > 0;
    }
    CHECK(high == std::array{1, 2, 4, 6}[duty]);
  }
  CHECK(gb::Voice::PeriodForIncrement(gb::Kind::Pulse, noteFrequency(69)) ==
        1750);
  CHECK(gb::Voice::PeriodForIncrement(gb::Kind::Wave, noteFrequency(69)) ==
        1899);
  voice.kind = gb::Kind::Pulse;
  voice.period = 1750;
  CHECK(voice.timerPeriod() == 1192);
}

TEST_CASE("GB noise implements NR43 divisors and both LFSR widths") {
  gb::Voice voice;
  voice.kind = gb::Kind::Noise;
  for (int shift = 0; shift < 14; ++shift)
    for (int divisor = 0; divisor < 8; ++divisor) {
      voice.noise = (shift << 4) | divisor;
      CHECK(voice.timerPeriod() ==
            unsigned((divisor ? divisor * 16 : 8) << shift));
    }
  voice.noise = 0xE0;
  CHECK(voice.timerPeriod() == 0xFFFFFFFFU);
  for (int width : {0, 8}) {
    voice.noise = width;
    voice.lfsr = 0x7FFF;
    unsigned expected = 0x7FFF;
    for (int i = 0; i < 128; ++i) {
      const unsigned bit = ((expected & 1) ^ ((expected >> 1) & 1));
      expected = (expected >> 1) | (bit << 14);
      if (width)
        expected = (expected & 0xFFBFU) | (bit << 6);
      voice.edge();
      CHECK(voice.lfsr == expected);
    }
  }
}

TEST_CASE("GB frame sequencer clocks length envelope and sweep separately") {
  gb::Voice voice;
  voice.envelope = 0xA1;
  voice.trigger(60, true);
  for (int i = 0; i < 7; ++i)
    voice.clockFrame();
  CHECK(voice.envelopeVolume == 10);
  voice.clockFrame();
  CHECK(voice.envelopeVolume == 9);
  voice.length = 2;
  voice.clockFrame();
  CHECK(voice.active);
  voice.clockFrame();
  CHECK(voice.active);
  voice.clockFrame();
  CHECK_FALSE(voice.active);
  voice = gb::Voice{};
  voice.sweep = 0x11;
  voice.trigger(69, true);
  CHECK_FALSE(voice.active); // Immediate sweep overflow at trigger.
}

TEST_CASE(
    "GB pitch FX are registered only for tonal voices and preserve presets") {
  for (auto kind : {fx::Instrument::GBWave, fx::Instrument::GBPulse}) {
    CHECK(fx::Available(FourCC::InstrumentCommandVibrato, {kind, false}));
    CHECK(fx::Available(FourCC::InstrumentCommandPitchSlide, {kind, false}));
    CHECK(fx::Available(FourCC::InstrumentCommandArpeggiator, {kind, false}));
  }
  CHECK_FALSE(fx::Available(FourCC::InstrumentCommandVibrato,
                            {fx::Instrument::GBNoise, false}));
  CHECK_FALSE(fx::Available(FourCC::InstrumentCommandRetrigger,
                            {fx::Instrument::GBWave, false}));
  CHECK(fx::InstrumentParameter(fx::Instrument::GBWave, 0x2F) != nullptr);
  CHECK(fx::InstrumentParameter(fx::Instrument::GBWave, 0x30) == nullptr);
  CHECK(fx::InstrumentParameter(fx::Instrument::GBNoise, 1) == nullptr);
  TrackVoicePool<gb::Voice> pool;
  GBPulseInstrument synth(&pool);
  REQUIRE(synth.Start(0, 60));
  const auto original = pool.voices[0].period;
  synth.ProcessCommand(0, FourCC::InstrumentCommandPitchSlide, 0x000C);
  CHECK(pool.voices[0].period > original);
  synth.ProcessCommand(0, FourCC::InstrumentCommandSetInstrumentParameter,
                       0x0003);
  CHECK(pool.voices[0].duty == 3);
  CHECK(synth.FindVariable(FourCC::GBDuty)->GetInt() == 2);
}
