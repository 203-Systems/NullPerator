#include "doctest/doctest.h"

#include "Services/Audio/AudioMixer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace {

class ConstantStereoModule final : public AudioModule {
public:
  ConstantStereoModule(int left, int right)
      : left_(i2fp(left)), right_(i2fp(right)) {}

  bool Render(fixed *buffer, int samplecount) override {
    for (int frame = 0; frame < samplecount; ++frame) {
      buffer[frame * 2] = left_;
      buffer[frame * 2 + 1] = right_;
    }
    return true;
  }

private:
  fixed left_;
  fixed right_;
};

} // namespace

TEST_CASE("AudioMixer reports stereo peak magnitudes in channel order") {
  AudioMixer mixer("test");
  ConstantStereoModule source(-1234, 2345);
  mixer.AddModule(source);
  std::array<fixed, 64U * 2U> buffer{};

  REQUIRE(mixer.Render(buffer.data(), 64));
  const stereosample levels = mixer.GetMixerLevels();
  CHECK(static_cast<std::uint16_t>(levels >> 16U) == 1234U);
  CHECK(static_cast<std::uint16_t>(levels & 0xFFFFU) == 2345U);
}

TEST_CASE(
    "Mixer preserves overflow cancellation before applying master volume") {
  AudioMixer::Workspace workspace;
  for (bool reverse : {false, true}) {
    AudioMixer mixer("test", &workspace);
    ConstantStereoModule positive(60000, -60000), negative(-60000, 60000);
    for (int i = 0; i < 5; ++i)
      REQUIRE(mixer.AddModule(reverse ? negative : positive));
    for (int i = 0; i < 4; ++i)
      REQUIRE(mixer.AddModule(reverse ? positive : negative));
    mixer.SetVolume(FP_ONE / 4);
    std::array<fixed, 130> buffer{};
    REQUIRE(mixer.Render(buffer.data(), 65));
    for (int i = 0; i < 65; ++i) {
      CHECK(buffer[2 * i] == i2fp(reverse ? -15000 : 15000));
      CHECK(buffer[2 * i + 1] == -buffer[2 * i]);
    }
  }
}

TEST_CASE("Packed stereo carries preserve maximum positive and negative sums") {
  for (bool opposite : {false, true}) {
    AudioMixer::Workspace workspace;
    AudioMixer mixer("test", &workspace);
    ConstantStereoModule source(65535, opposite ? -65536 : 65535);
    for (int i = 0; i < 10; ++i)
      REQUIRE(mixer.AddModule(source));
    mixer.SetVolume(FP_ONE / 16);
    std::array<fixed, 14> output{};
    REQUIRE(mixer.Render(output.data(), 7));
    const auto expected = [](int sample) {
      return static_cast<fixed>(std::int64_t(sample) * FP_ONE * 10 / 16);
    };
    for (int i = 0; i < 7; ++i) {
      CHECK(output[2 * i] == expected(65535));
      CHECK(output[2 * i + 1] == expected(opposite ? -65536 : 65535));
    }
  }
}

TEST_CASE("Packed carries match a wide reference for fractional Q15 inputs") {
  struct Source final : AudioModule {
    fixed values[2];
    bool Render(fixed *buffer, int count) override {
      for (int i = 0; i < count * 2; ++i)
        buffer[i] = values[i & 1];
      return true;
    }
  };
  AudioMixer::Workspace workspace;
  AudioMixer mixer("test", &workspace);
  std::array<Source, 10> sources;
  for (auto &source : sources)
    REQUIRE(mixer.AddModule(source));
  std::uint32_t random = 12345;
  for (int scenario = 0; scenario < 256; ++scenario) {
    std::int64_t sums[2]{};
    for (auto &source : sources) {
      for (int channel = 0; channel < 2; ++channel) {
        random = random * 1664525U + 1013904223U;
        const fixed sample = scenario < 4
                                 ? ((scenario >> channel) & 1
                                        ? std::numeric_limits<fixed>::max()
                                        : std::numeric_limits<fixed>::min())
                                 : static_cast<fixed>(random);
        source.values[channel] = sample;
        sums[channel] += sample;
      }
    }
    for (fixed gain : {0, 1, FP_ONE / 16, FP_ONE / 3, FP_ONE}) {
      mixer.SetVolume(gain);
      std::array<fixed, 2> output{};
      REQUIRE(mixer.Render(output.data(), 1));
      for (int channel = 0; channel < 2; ++channel) {
        const auto scaled = (sums[channel] * gain) >> FIXED_SHIFT;
        const auto expected =
            std::clamp<std::int64_t>(scaled, std::numeric_limits<fixed>::min(),
                                     std::numeric_limits<fixed>::max());
        CHECK(output[channel] == expected);
      }
    }
  }
}

TEST_CASE("Nested branching mixers have independent accumulation workspaces") {
  AudioMixer::Workspace parentWorkspace, childWorkspace;
  AudioMixer parent("parent", &parentWorkspace),
      child("child", &childWorkspace);
  ConstantStereoModule a(100, 200), b(300, 400), c(500, 600);
  REQUIRE(parent.AddModule(a));
  REQUIRE(parent.AddModule(child));
  REQUIRE(child.AddModule(b));
  REQUIRE(child.AddModule(c));
  std::array<fixed, 2> output{};
  REQUIRE(parent.Render(output.data(), 1));
  CHECK(output[0] == i2fp(900));
  CHECK(output[1] == i2fp(1200));
  AudioMixer noWorkspace("leaf");
  REQUIRE(noWorkspace.AddModule(a));
  CHECK_FALSE(noWorkspace.AddModule(b));
  CHECK_FALSE(parent.AddModule(parent));
}

namespace {
class CountingSequence final : public AudioModule {
public:
  bool active = true;
  int calls = 0;
  int frames = 0;
  int seed = 0;
  bool Render(fixed *buffer, int count) override {
    ++calls;
    // Full-width Q15 values exercise carry cancellation and child saturation.
    for (int i = 0; i < count * 2; ++i) {
      const std::uint32_t bits =
          (static_cast<std::uint32_t>(frames * 2 + i + seed) * 2654435761U);
      buffer[i] = static_cast<fixed>(bits);
    }
    frames += count;
    return active;
  }
};
struct MixGraph {
  AudioMixer::Workspace masterWorkspace, previewWorkspace;
  AudioMixer master;
  AudioMixer preview;
  std::array<CountingSequence, 10> sources;
  explicit MixGraph(bool reuse)
      : master("master", &masterWorkspace),
        preview("preview", reuse ? &masterWorkspace : &previewWorkspace) {
    for (int i = 0; i < 10; ++i)
      sources[i].seed = i * 91;
    REQUIRE(preview.AddModule(sources[8]));
    REQUIRE(preview.AddModule(sources[9]));
    if (reuse)
      REQUIRE(master.AddModule(preview));
    for (int i = 0; i < 8; ++i)
      REQUIRE(master.AddModule(sources[i]));
    if (!reuse)
      REQUIRE(master.AddModule(preview));
  }
};
} // namespace

TEST_CASE("First-child scratch reuse matches independent full-block mixing") {
  static_assert(sizeof(AudioMixer::Workspace) <= MAX_SAMPLE_COUNT * 9U + 33U);
  MixGraph independent(false), reused(true);
  std::array<fixed, MAX_SAMPLE_COUNT * 2 + 2> expected{}, actual{};
  for (int previewMask : {3, 0, 1, 2, 3}) {
    for (bool tracksActive : {true, false}) {
      for (fixed volume : {FP_ONE, FP_ONE / 4, 0}) {
        for (int count : {1, 65, 512, MAX_SAMPLE_COUNT}) {
          independent.master.SetVolume(volume);
          reused.master.SetVolume(volume);
          for (int i = 0; i < 10; ++i) {
            const bool active =
                i < 8 ? tracksActive : (previewMask & (1 << (i - 8))) != 0;
            independent.sources[i].active = reused.sources[i].active = active;
          }
          expected.fill(123);
          actual.fill(123);
          const bool gotData =
              independent.master.Render(expected.data(), count);
          REQUIRE(reused.master.Render(actual.data(), count) == gotData);
          if (gotData)
            for (int i = 0; i < count * 2; ++i)
              CHECK(actual[i] == expected[i]);
          CHECK(actual[count * 2] == 123);
          CHECK(actual[count * 2 + 1] == 123);
          CHECK(reused.master.GetMixerLevels() ==
                independent.master.GetMixerLevels());
          for (int i = 0; i < 10; ++i) {
            CHECK(reused.sources[i].calls == independent.sources[i].calls);
            CHECK(reused.sources[i].frames == independent.sources[i].frames);
          }
          CHECK_FALSE(reused.master.RenderFailed());
        }
      }
    }
  }
}

TEST_CASE("Overlapping workspace reuse fails before touching child sources") {
  AudioMixer::Workspace workspace;
  AudioMixer parent("parent", &workspace), child("child", &workspace);
  CountingSequence first, a, b;
  REQUIRE(child.AddModule(a));
  REQUIRE(child.AddModule(b));
  REQUIRE(parent.AddModule(first));
  REQUIRE(
      parent.AddModule(child)); // Invalid: parent's scratch is already live.
  std::array<fixed, 130> output{};
  CHECK_FALSE(parent.Render(output.data(), 65));
  CHECK(parent.RenderFailed());
  CHECK(child.RenderFailed());
  CHECK(a.calls == 0);
  CHECK(b.calls == 0);
  // A rejected render must release ownership for the next block.
  parent.ClearModules();
  REQUIRE(parent.AddModule(child));
  REQUIRE(parent.AddModule(first));
  parent.ClearRenderError();
  child.ClearRenderError();
  CHECK(parent.Render(output.data(), 65));
  CHECK_FALSE(parent.RenderFailed());
  CHECK_FALSE(child.RenderFailed());
  CHECK(a.calls == 1);
  CHECK(b.calls == 1);
}

#include "Services/Audio/AudioOutDriver.h"
namespace {
class CapturingDriver final : public AudioDriver {
public:
  explicit CapturingDriver(AudioSettings &settings) : AudioDriver(settings) {}
  bool InitDriver() override { return true; }
  void CloseDriver() override {}
  bool StartDriver() override { return true; }
  void StopDriver() override {}
  bool Interlaced() override { return interlaced; }
  int GetPlayedBufferPercentage() override { return 0; }
  double GetStreamTime() override { return 0; }
  std::span<short> GetOutputBuffer() override { return {pcm.data(), capacity}; }
  void AddBuffer(short *buffer, int size) override {
    CHECK(buffer == pcm.data());
    ++submissions;
    frames = size;
  }
  bool interlaced = true;
  int frames = 0;
  std::array<short, 16> pcm{};
  std::size_t capacity = pcm.size();
  int submissions = 0;
};
} // namespace
TEST_CASE("PCM output clips both layouts and carries fractional clock frames") {
  AudioSettings settings{};
  CapturingDriver driver(settings);
  AudioOutDriver output(driver);
  ConstantStereoModule source(40000, -40000);
  REQUIRE(output.AddModule(source));
  output.SetFrameClock(+[] { return 5.5F; });
  for (bool interlaced : {true, false}) {
    driver.interlaced = interlaced;
    output.Trigger();
    CHECK(driver.frames == (interlaced ? 5 : 6));
    for (int i = 0; i < driver.frames; ++i) {
      CHECK(driver.pcm[interlaced ? 2 * i : i] == 32767);
      CHECK(driver.pcm[interlaced ? 2 * i + 1 : driver.frames + i] == -32768);
    }
  }
  output.SetFrameClock(nullptr);
  output.Trigger();
  CHECK(driver.frames == 0);
}

TEST_CASE("PCM output requires enough driver-owned storage before rendering") {
  AudioSettings settings{};
  CapturingDriver driver(settings);
  AudioOutDriver output(driver);
  CountingSequence source;
  REQUIRE(output.AddModule(source));
  output.SetFrameClock(+[] { return 5.0F; });
  driver.pcm.fill(1234);
  for (std::size_t capacity : {0U, 9U}) {
    driver.capacity = capacity;
    output.Trigger();
    CHECK(source.calls == 0);
    CHECK(driver.submissions == 0);
    for (short sample : driver.pcm)
      CHECK(sample == 1234);
  }
  driver.capacity = 10;
  output.Trigger();
  CHECK(source.calls == 1);
  CHECK(driver.submissions == 1);
  CHECK(driver.frames == 5);
  CHECK(driver.pcm[10] == 1234);
  CHECK(driver.pcm[15] == 1234);
  source.active = false;
  output.Trigger();
  for (int i = 0; i < 10; ++i)
    CHECK(driver.pcm[i] == 0);
}
