#include "doctest/doctest.h"

#include "Services/Audio/AudioMixer.h"

#include <array>
#include <cstdint>

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
