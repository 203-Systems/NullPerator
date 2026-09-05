#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

#include "Application/Instruments/Filters.h"
#include "sample_instrument_test_peer.h"

namespace {
class RenderSource final : public SoundSource {
public:
  explicit RenderSource(int frames, int channels = 1)
      : channels(channels), pcm(frames * channels, 1000) {}
  int GetSize(int) override { return static_cast<int>(pcm.size()) / channels; }
  int GetSampleRate(int) override { return 44100; }
  int GetChannelCount(int) override { return channels; }
  void *GetSampleBuffer(int) override { return pcm.data(); }
  bool IsMulti() override { return false; }
  int GetRootNote(int) override { return 60; }
  float GetLengthInSec() override { return GetSize(0) / 44100.0F; }
  int channels;
  std::vector<short> pcm;
};

void Bind(SampleInstrument &instrument, RenderSource &source) {
  SampleInstrumentTestPeer::BindSource(instrument, source);
  instrument.FindVariable(FourCC::SampleInstrumentEnd)
      ->SetInt(source.GetSize(0));
  init_filters();
  for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    *get_filter(channel) = {};
  }
}
} // namespace

TEST_CASE("Sampler stereo ordering agrees between unity and general paths") {
  RenderSource source(32, 2);
  for (int i = 0; i < 32; ++i) {
    source.pcm[2 * i] = 1000;
    source.pcm[2 * i + 1] = -2000;
  }
  SampleInstrument instrument;
  Bind(instrument, source);
  std::array<fixed, 16> fast{}, general{};
  REQUIRE(instrument.Start(0, 60));
  REQUIRE(instrument.Render(0, fast.data(), 8, false));
  REQUIRE(instrument.Start(0, 60));
  // A no-op pan updater selects the general path without changing the sound.
  instrument.ProcessCommand(0, FourCC::InstrumentCommandPan, 0x007F);
  REQUIRE(instrument.Render(0, general.data(), 8, false));
  CHECK(general == fast);
  CHECK(general[0] > 0);
  CHECK(general[1] < 0);
}

TEST_CASE("Sampler one-shot emits every frame including a one-frame sample") {
  for (int length : {1, 2, 8}) {
    CAPTURE(length);
    RenderSource source(length);
    SampleInstrument instrument;
    Bind(instrument, source);
    REQUIRE(instrument.Start(0, 60));
    std::array<fixed, 20> output{};
    REQUIRE(instrument.Render(0, output.data(), 10, false));
    for (int frame = 0; frame < length; ++frame)
      CHECK(output[frame * 2] > 0);
    for (int frame = length; frame < 10; ++frame)
      CHECK(output[frame * 2] == 0);
    CHECK(SampleInstrumentTestPeer::Params(0).finished_);
  }
}

TEST_CASE("Sampler automation is independent of render buffer partitioning") {
  RenderSource source(1024);
  SampleInstrument instrument;
  Bind(instrument, source);
  std::array<fixed, 1200> whole{}, chunks{};
  REQUIRE(instrument.Start(0, 60));
  instrument.ProcessCommand(0, FourCC::InstrumentCommandVolume, 0x1000);
  REQUIRE(instrument.Render(0, whole.data(), 600, false));
  REQUIRE(instrument.Start(0, 60));
  instrument.ProcessCommand(0, FourCC::InstrumentCommandVolume, 0x1000);
  for (int offset = 0; offset < 600; offset += 60)
    REQUIRE(instrument.Render(0, chunks.data() + offset * 2, 60, false));
  CHECK(chunks == whole);
}

TEST_CASE("Sampler automation advances on the next hundred-frame boundary") {
  RenderSource source(256);
  SampleInstrument instrument;
  Bind(instrument, source);
  REQUIRE(instrument.Start(0, 60));
  instrument.ProcessCommand(0, FourCC::InstrumentCommandVolume, 0x1000);
  std::array<fixed, 200> output{};
  REQUIRE(instrument.Render(0, output.data(), 100, false));
  const fixed before = SampleInstrumentTestPeer::Params(0).volume_;
  REQUIRE(instrument.Render(0, output.data(), 1, false));
  CHECK(SampleInstrumentTestPeer::Params(0).volume_ < before);
}

TEST_CASE(
    "Sampler filter command takes effect inside its first render buffer") {
  RenderSource source(256);
  SampleInstrument instrument;
  Bind(instrument, source);
  std::array<fixed, 64> automated{}, configured{};
  REQUIRE(instrument.Start(0, 60));
  instrument.ProcessCommand(0, FourCC::InstrumentCommandFilterCut, 0x0000);
  REQUIRE(instrument.Render(0, automated.data(), 32, false));
  *get_filter(0) = {};
  instrument.FindVariable(FourCC::SampleInstrumentFilterCutOff)->SetInt(0);
  REQUIRE(instrument.Start(0, 60));
  REQUIRE(instrument.Render(0, configured.data(), 32, false));
  CHECK(automated == configured);
}

TEST_CASE("Sampler ping-pong contains overshoot at high pitch") {
  RenderSource source(8);
  SampleInstrument instrument;
  Bind(instrument, source);
  instrument.FindVariable(FourCC::SampleInstrumentLoopMode)
      ->SetInt(SILM_LOOP_PINGPONG);
  REQUIRE(instrument.Start(0, 108)); // 16 source frames per output frame
  std::array<fixed, 64> output{};
  REQUIRE(instrument.Render(0, output.data(), 32, false));
  for (fixed sample : output)
    CHECK(sample == output[0]);
  CHECK(output[0] > 0);
}

TEST_CASE("Sampler reverse start at the end stays within the source") {
  RenderSource source(8);
  SampleInstrument instrument;
  Bind(instrument, source);
  instrument.FindVariable(FourCC::SampleInstrumentStart)->SetInt(8);
  instrument.FindVariable(FourCC::SampleInstrumentEnd)->SetInt(0);
  REQUIRE(instrument.Start(0, 60));
  std::array<fixed, 20> output{};
  REQUIRE(instrument.Render(0, output.data(), 10, false));
  for (int frame = 0; frame < 8; ++frame)
    CHECK(output[frame * 2] > 0);
  CHECK(output[16] == 0);
}

TEST_CASE("Sampler dry kernels match general output for pitched PCM") {
  for (int channels : {1, 2}) {
    for (int interpolation : {0, 1}) {
      for (int note : {36, 60, 67, 84}) {
        CAPTURE(channels);
        CAPTURE(interpolation);
        CAPTURE(note);
        RenderSource source(256, channels);
        for (std::size_t i = 0; i < source.pcm.size(); ++i)
          source.pcm[i] = static_cast<short>((i * 7919U) % 65536U - 32768);
        SampleInstrument instrument;
        Bind(instrument, source);
        instrument.FindVariable(FourCC::SampleInstrumentInterpolation)
            ->SetInt(interpolation);
        instrument.FindVariable(FourCC::SampleInstrumentLoopMode)
            ->SetInt(SILM_LOOP);
        std::array<fixed, 1024> fast{}, general{};
        REQUIRE(instrument.Start(0, note));
        REQUIRE(instrument.Render(0, fast.data(), 512, false));
        REQUIRE(instrument.Start(0, note));
        instrument.ProcessCommand(0, FourCC::InstrumentCommandPan, 0x007F);
        REQUIRE(instrument.Render(0, general.data(), 512, false));
        CHECK(fast == general);
      }
    }
  }
}

TEST_CASE(
    "Sampler short loop modes and downsampling stay in their allocation") {
  for (int channels : {1, 2}) {
    for (int length : {1, 2, 3, 7}) {
      for (int mode = SILM_ONESHOT; mode < SILM_LAST; ++mode) {
        for (bool reverse : {false, true}) {
          for (int downsample : {0, 8}) {
            CAPTURE(channels);
            CAPTURE(length);
            CAPTURE(mode);
            CAPTURE(reverse);
            CAPTURE(downsample);
            RenderSource source(length, channels);
            SampleInstrument instrument;
            Bind(instrument, source);
            instrument.FindVariable(FourCC::SampleInstrumentLoopMode)
                ->SetInt(mode);
            instrument.FindVariable(FourCC::SampleInstrumentDownsample)
                ->SetInt(downsample);
            if (reverse) {
              instrument.FindVariable(FourCC::SampleInstrumentStart)
                  ->SetInt(length);
              instrument.FindVariable(FourCC::SampleInstrumentLoopStart)
                  ->SetInt(length);
              instrument.FindVariable(FourCC::SampleInstrumentEnd)->SetInt(0);
            }
            REQUIRE(instrument.Start(0, 120));
            std::array<fixed, 64> output{};
            for (int block = 0; block < 3; ++block)
              instrument.Render(0, output.data(), 32, false);
          }
        }
      }
    }
  }
}

TEST_CASE("Sampler forward loop preserves pitch overshoot") {
  RenderSource source(7);
  for (int i = 0; i < 7; ++i)
    source.pcm[i] = static_cast<short>(1000 * (i + 1));
  SampleInstrument instrument;
  Bind(instrument, source);
  instrument.FindVariable(FourCC::SampleInstrumentLoopMode)->SetInt(SILM_LOOP);
  REQUIRE(instrument.Start(0, 72));
  std::array<fixed, 14> output{};
  REQUIRE(instrument.Render(0, output.data(), 7, false));
  const std::array<int, 7> expected{1, 3, 5, 7, 2, 4, 6};
  for (int i = 0; i < 7; ++i)
    CHECK(output[i * 2] == expected[i] * output[0]);
}

TEST_CASE("Sampler filter mapping changes without a cutoff change") {
  init_filters();
  *get_filter(0) = {};
  set_filter(0, FLT_LOWPASS, FP_ONE / 2, 0, 0, false);
  const fixed normal = get_filter(0)->freq;
  set_filter(0, FLT_LOWPASS, FP_ONE / 2, 0, 0, true);
  CHECK(get_filter(0)->freq != normal);
  set_filter(0, FLT_LOWPASS, FP_ONE / 2, 0, 0, false);
  CHECK(get_filter(0)->freq == normal);
}

TEST_CASE(
    "Sampler filtered spans match general output and retain filter state") {
  for (int channels : {1, 2}) {
    for (int interpolation : {0, 1}) {
      for (int mode : {0, 1, 2}) {
        CAPTURE(channels);
        CAPTURE(interpolation);
        CAPTURE(mode);
        RenderSource source(67, channels);
        for (std::size_t i = 0; i < source.pcm.size(); ++i)
          source.pcm[i] = static_cast<short>((i * 7919U) % 2000U - 1000);
        SampleInstrument instrument;
        Bind(instrument, source);
        instrument.FindVariable(FourCC::SampleInstrumentInterpolation)
            ->SetInt(interpolation);
        instrument.FindVariable(FourCC::SampleInstrumentLoopMode)
            ->SetInt(SILM_LOOP);
        instrument.FindVariable(FourCC::SampleInstrumentFilterMode)
            ->SetInt(mode);
        instrument.FindVariable(FourCC::SampleInstrumentFilterCutOff)
            ->SetInt(160);
        instrument.FindVariable(FourCC::SampleInstrumentFilterResonance)
            ->SetInt(96);
        instrument.FindVariable(FourCC::SampleInstrumentFilterType)->SetInt(64);
        std::array<fixed, 1024> fast{}, general{};
        REQUIRE(instrument.Start(0, 67));
        for (int block = 0; block < 4; ++block)
          REQUIRE(instrument.Render(0, fast.data() + block * 256, 128, false));
        Bind(instrument, source);
        REQUIRE(instrument.Start(0, 67));
        instrument.ProcessCommand(0, FourCC::InstrumentCommandPan, 0x007F);
        for (int block = 0; block < 4; ++block)
          REQUIRE(
              instrument.Render(0, general.data() + block * 256, 128, false));
        CHECK(fast == general);
      }
    }
  }
}

TEST_CASE("Long pitched samples preserve every fractional bit across blocks") {
  const int start = (1 << 22) + 3;
  RenderSource source(start + 1024);
  for (int i = start - 4; i < source.GetSize(0); ++i)
    source.pcm[i] = static_cast<short>((i % 113) * 211 - 12000);
  SampleInstrument instrument;
  Bind(instrument, source);
  std::array<fixed, 800> whole{}, chunks{};
  for (bool reverse : {false, true}) {
    auto prepare = [&] {
      REQUIRE(instrument.Start(0, 61));
      auto &params = SampleInstrumentTestPeer::Params(0);
      params.positionPhase_ =
          std::int64_t(start + (reverse ? 500 : 0)) * FP_ONE + 7;
      params.reverse_ = reverse;
      if (reverse)
        params.rendLoopEnd_ = 0;
    };
    prepare();
    REQUIRE(instrument.Render(0, whole.data(), 400, false));
    const auto finalPhase = SampleInstrumentTestPeer::Params(0).positionPhase_;
    prepare();
    for (int offset = 0; offset < 400; offset += 5)
      REQUIRE(instrument.Render(0, chunks.data() + 2 * offset, 5, false));
    CHECK(chunks == whole);
    CHECK(SampleInstrumentTestPeer::Params(0).positionPhase_ == finalPhase);
  }
}

TEST_CASE("Sample offsets retain exact Q15 phase on long files") {
  RenderSource source((1 << 22) + 13);
  SampleInstrument instrument;
  Bind(instrument, source);
  REQUIRE(instrument.Start(0, 60));
  const auto chunk = std::int64_t(source.GetSize(0)) * FP_ONE / 256;
  instrument.ProcessCommand(0, FourCC::InstrumentCommandPlayOfset, 0x80FF);
  CHECK(SampleInstrumentTestPeer::Params(0).positionPhase_ == chunk * 127);
  instrument.ProcessCommand(0, FourCC::InstrumentCommandPlayOfset, 0x0001);
  CHECK(SampleInstrumentTestPeer::Params(0).positionPhase_ == chunk * 128);
}

TEST_CASE(
    "Reverse loops retain a negative overshoot across callback boundaries") {
  RenderSource source(32);
  for (int i = 0; i < 32; ++i)
    source.pcm[i] = static_cast<short>(i * 101 - 1000);
  SampleInstrument instrument;
  Bind(instrument, source);
  instrument.FindVariable(FourCC::SampleInstrumentStart)->SetInt(31);
  instrument.FindVariable(FourCC::SampleInstrumentEnd)->SetInt(0);
  for (int mode : {int(SILM_LOOP), int(SILM_LOOP_PINGPONG)}) {
    instrument.FindVariable(FourCC::SampleInstrumentLoopMode)->SetInt(mode);
    std::array<fixed, 160> whole{}, chunks{};
    REQUIRE(instrument.Start(0, 109));
    REQUIRE(instrument.Render(0, whole.data(), 80, false));
    const auto phase = SampleInstrumentTestPeer::Params(0).positionPhase_;
    REQUIRE(instrument.Start(0, 109));
    for (int i = 0; i < 80; ++i)
      REQUIRE(instrument.Render(0, chunks.data() + 2 * i, 1, false));
    CHECK(chunks == whole);
    CHECK(SampleInstrumentTestPeer::Params(0).positionPhase_ == phase);
  }
}
