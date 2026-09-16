#include "Application/Instruments/ChiptuneInstrument.h"
#include "Application/Instruments/GBInstrument.h"
#include "Application/Instruments/Filters.h"
#include "Foundation/Types/SynthLevel.h"
#include "Externals/copingSynth/DrumInstrument/DrumEngine.h"
#include "Externals/copingSynth/StackInstrument/StackEngine.h"
#include "sample_instrument_test_peer.h"
#include "doctest/doctest.h"
#include <array>
#include <cmath>
#include <vector>

namespace {
class FullScaleSquare final : public SoundSource {
public:
  FullScaleSquare() : pcm(16384) {
    for (int n = 0; n < int(pcm.size()); ++n)
      pcm[n] = ((n * 440) % 44100) < 22050 ? 32767 : -32767;
  }
  int GetSize(int) override { return int(pcm.size()); }
  int GetSampleRate(int) override { return 44100; }
  int GetChannelCount(int) override { return 1; }
  void *GetSampleBuffer(int) override { return pcm.data(); }
  bool IsMulti() override { return false; }
  int GetRootNote(int) override { return 60; }
  float GetLengthInSec() override { return pcm.size() / 44100.0F; }
  std::vector<short> pcm;
};

double Rms(I_Instrument &instrument) {
  double energy = 0;
  std::array<fixed, 512> buffer{};
  // Settle DC removal and attack, then measure 6144 frames before sample end.
  for (int block = 0; block < 32; ++block) {
    REQUIRE(instrument.Render(0, buffer.data(), 256, false));
    if (block < 8)
      continue;
    for (int frame = 0; frame < 256; ++frame) {
      const double pcm = double(buffer[frame * 2]) / FP_ONE;
      energy += pcm * pcm;
    }
  }
  return std::sqrt(energy / 6144);
}

double SampleRms(int volume) {
  FullScaleSquare source;
  SampleInstrument sample;
  SampleInstrumentTestPeer::BindSource(sample, source);
  sample.FindVariable(FourCC::SampleInstrumentEnd)->SetInt(source.GetSize(0));
  sample.FindVariable(FourCC::SampleInstrumentVolume)->SetInt(volume);
  init_filters();
  *get_filter(0) = {};
  REQUIRE(sample.Start(0, 60)); // root pitch: PCM already contains A4
  return Rms(sample);
}
} // namespace

TEST_CASE("Synth output level agrees with the actual centered Sample renderer") {
  for (int volume : {64, 128, 255}) {
    CAPTURE(volume);
    const double reference = SampleRms(volume);
    // Protect the existing Sample level, not only the relative comparison.
    CHECK(reference == doctest::Approx(11584.65 * volume / 128).epsilon(.005));

    GBPulseInstrument pulse;
    pulse.FindVariable(FourCC::GBVolume)->SetInt(volume);
    pulse.FindVariable(FourCC::GBEnvelope)->SetInt(0xF0);
    REQUIRE(pulse.Start(0, 69));
    CHECK(Rms(pulse) == doctest::Approx(reference).epsilon(.04));

    GBWaveInstrument wave;
    wave.FindVariable(FourCC::GBVolume)->SetInt(volume);
    for (int word = 0; word < 8; ++word)
      wave.FindVariable(static_cast<FourCC::enum_type>(FourCC::GBWave0 + word))
          ->SetInt(word < 4 ? 0xFFFF : 0);
    REQUIRE(wave.Start(0, 69));
    CHECK(Rms(wave) == doctest::Approx(reference).epsilon(.04));

    ChiptuneInstrument chip;
    chip.FindVariable(FourCC::ChiptuneWave)->SetInt(coping::chip::wavePulse50);
    chip.FindVariable(FourCC::ChiptuneVolume)->SetInt(volume);
    chip.FindVariable(FourCC::ChiptuneAttack)->SetInt(0);
    chip.FindVariable(FourCC::ChiptuneDecay)->SetInt(255);
    chip.FindVariable(FourCC::ChiptuneVibratoDepth)->SetInt(0);
    REQUIRE(chip.Start(0, 69));
    CHECK(Rms(chip) == doctest::Approx(reference).epsilon(.06));
  }
}

TEST_CASE("GB noise volume remains linear and mute remains exact after calibration") {
  std::array<double, 3> levels{};
  for (int i = 0; i < 3; ++i) {
    GBNoiseInstrument noise;
    noise.FindVariable(FourCC::GBVolume)->SetInt(i * 64);
    noise.FindVariable(FourCC::GBEnvelope)->SetInt(0xF0);
    noise.FindVariable(FourCC::GBNoise)->SetInt(0x53);
    REQUIRE(noise.Start(0, 60));
    levels[i] = Rms(noise);
  }
  CHECK(levels[0] == 0);
  CHECK(levels[2] > 5000);
  CHECK(levels[2] == doctest::Approx(2 * levels[1]).epsilon(.001));
}

TEST_CASE("Synth calibration has bounded full-scale peaks without changing silence") {
  CHECK(SynthLevel::Coping(0) == 0);
  CHECK(SynthLevel::Stack(0) == 0);
  CHECK(SynthLevel::Coping(1 << 27) == SynthLevel::CenteredFixedPeak);
  CHECK(SynthLevel::Stack(SynthLevel::StackNominalPeak) ==
        doctest::Approx(double(SynthLevel::CenteredFixedPeak)).epsilon(.0001));
  CHECK(SynthLevel::Coping(-(1 << 27)) == -SynthLevel::CenteredFixedPeak);
  CHECK(SynthLevel::Stack(-SynthLevel::StackNominalPeak) ==
        -SynthLevel::Stack(SynthLevel::StackNominalPeak));
}

TEST_CASE("Drum and coherent Stack square peaks use the centered Sample scale") {
  for (int volume : {128, 255}) {
    CAPTURE(volume);
    drum_voice_t drum{};
    drum_parameters_t dp{};
    dp.wave = drumWavePulse50;
    dp.decay = 15;
    dp.note = 6;
    drum.note_on(60, volume, true, dp);
    drum.envelope.decay = 0; // isolate gain from the per-drum decay setting
    stack_voice_t stack{};
    stack_parameters_t sp{};
    sp.wave = stackWavePulse50;
    sp.volume = volume;
    sp.sustain = 255;
    sp.brightness = 12;
    stack.note_on(69, 255, true, sp);
    double drumPeak = 0, stackPeak = 0;
    for (int i = 0; i < 8192; ++i) {
      fixed left, right;
      drum.sample(&left, &right);
      drumPeak = std::max(drumPeak, std::abs(double(left)) / FP_ONE);
      stack.sample(&left, &right);
      stackPeak = std::max(stackPeak, std::abs(double(left)) / FP_ONE);
    }
    const double expected = SynthLevel::CenteredPcmPeak * volume / 255.0;
    CHECK(drumPeak == doctest::Approx(expected).epsilon(.04));
    CHECK(stackPeak == doctest::Approx(expected).epsilon(.04));
  }
}
