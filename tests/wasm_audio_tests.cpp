#include "Adapters/wasm/audio/OutputResampler.h"
#include "Adapters/wasm/audio/PcmRingBuffer.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <atomic>
#include <thread>
#include <vector>

namespace {
StereoI16 Frame(std::int16_t left, std::int16_t right) {
  return StereoI16{left, right};
}

void CheckFrame(const StereoF32 &frame, float left, float right) {
  CHECK(frame.left == doctest::Approx(left));
  CHECK(frame.right == doctest::Approx(right));
}
} // namespace

TEST_CASE("PCM ring preserves frame ordering across wrap and distinguishes full from empty") {
  PcmRingBuffer<8> ring;
  std::array<StereoI16, 11> input{};
  for (std::size_t index = 0; index < input.size(); ++index) {
    input[index] = Frame(static_cast<std::int16_t>(index * 1000),
                         static_cast<std::int16_t>(-static_cast<int>(index) * 1000));
  }

  CHECK(ring.Write({input.data(), 8}) == 8);
  CHECK(ring.FillFrames() == 8);
  CHECK(ring.Write({input.data() + 8, 1}) == 0);
  CHECK(ring.Overruns() == 1);

  std::array<StereoF32, 3> first{};
  CHECK(ring.Read(first) == 3);
  CHECK(ring.FillFrames() == 5);
  for (std::size_t index = 0; index < first.size(); ++index) {
    CheckFrame(first[index], static_cast<float>(index * 1000) / 32768.0F,
               -static_cast<float>(index * 1000) / 32768.0F);
  }

  CHECK(ring.Write({input.data() + 8, 3}) == 3);
  CHECK(ring.FillFrames() == 8);
  std::array<StereoF32, 8> rest{};
  CHECK(ring.Read(rest) == rest.size());
  for (std::size_t index = 0; index < rest.size(); ++index) {
    const std::size_t sourceIndex = index + 3;
    CheckFrame(rest[index], static_cast<float>(sourceIndex * 1000) / 32768.0F,
               -static_cast<float>(sourceIndex * 1000) / 32768.0F);
  }
  CHECK(ring.FillFrames() == 0);
}

TEST_CASE("PCM ring rejects only excess producer frames and records rejected frames") {
  PcmRingBuffer<4> ring;
  const std::array<StereoI16, 6> input{Frame(1, -1), Frame(2, -2),
                                       Frame(3, -3), Frame(4, -4),
                                       Frame(5, -5), Frame(6, -6)};

  CHECK(ring.Write({input.data(), 3}) == 3);
  CHECK(ring.Write({input.data() + 3, 3}) == 1);
  CHECK(ring.FillFrames() == 4);
  CHECK(ring.Overruns() == 2);
}

TEST_CASE("PCM ring fills underruns with silence and counts missing frames") {
  PcmRingBuffer<4> ring;
  const std::array<StereoI16, 2> input{Frame(INT16_MIN, INT16_MAX),
                                       Frame(0, 123)};
  CHECK(ring.Write(input) == input.size());

  std::array<StereoF32, 4> output{};
  CHECK(ring.Read(output) == input.size());
  CheckFrame(output[0], -1.0F, static_cast<float>(INT16_MAX) / 32768.0F);
  CheckFrame(output[1], 0.0F, 123.0F / 32768.0F);
  CheckFrame(output[2], 0.0F, 0.0F);
  CheckFrame(output[3], 0.0F, 0.0F);
  CHECK(ring.Underruns() == 2);
}

TEST_CASE("PCM ring accepts engine interleaved shorts without aliasing stereo storage") {
  PcmRingBuffer<4> ring;
  const std::array<short, 6> input{
      std::numeric_limits<short>::min(), std::numeric_limits<short>::max(),
      0, -1, 123, -456};

  CHECK(ring.WriteInterleaved(input) == 3);
  std::array<StereoF32, 3> output{};
  CHECK(ring.Read(output) == 3);
  CheckFrame(output[0], -1.0F,
             static_cast<float>(std::numeric_limits<short>::max()) / 32768.0F);
  CheckFrame(output[1], 0.0F, -1.0F / 32768.0F);
  CheckFrame(output[2], 123.0F / 32768.0F, -456.0F / 32768.0F);
}

TEST_CASE("PCM ring rejects an odd interleaved sample count without changing order") {
  PcmRingBuffer<4> ring;
  const std::array<short, 3> malformed{1, 2, 3};
  const std::array<short, 4> valid{4, 5, 6, 7};

  CHECK(ring.WriteInterleaved(malformed) == 0);
  CHECK(ring.FillFrames() == 0);
  CHECK(ring.WriteInterleaved(valid) == 2);
  std::array<StereoF32, 2> output{};
  CHECK(ring.Read(output) == 2);
  CheckFrame(output[0], 4.0F / 32768.0F, 5.0F / 32768.0F);
  CheckFrame(output[1], 6.0F / 32768.0F, 7.0F / 32768.0F);
}

TEST_CASE("PCM ring reset clears positions and counters") {
  PcmRingBuffer<2> ring;
  const std::array<StereoI16, 2> input{Frame(100, 200), Frame(300, 400)};
  CHECK(ring.Write(input) == 2);
  std::array<StereoF32, 3> output{};
  CHECK(ring.Read(output) == 2);
  CHECK(ring.Underruns() == 1);
  ring.Reset();

  CHECK(ring.FillFrames() == 0);
  CHECK(ring.Underruns() == 0);
  CHECK(ring.Overruns() == 0);
  CHECK(ring.Write(input) == 2);
}

TEST_CASE("PCM ring uses wide monotonic positions and remains ordered under SPSC contention") {
  static_assert(sizeof(PcmRingBuffer<8>::Position) == sizeof(std::uint64_t));
  CHECK(std::numeric_limits<PcmRingBuffer<8>::Position>::max() >
        PcmRingBuffer<8>::Capacity);

  constexpr std::size_t frameCount = 65536;
  PcmRingBuffer<64> ring;
  std::atomic<bool> producerDone{false};
  std::atomic<bool> ordered{true};

  std::thread producer([&] {
    for (std::size_t index = 0; index < frameCount; ++index) {
      const StereoI16 frame = Frame(static_cast<std::int16_t>(index),
                                    static_cast<std::int16_t>(~index));
      while (ring.Write({&frame, 1}) == 0U) {
        std::this_thread::yield();
      }
    }
    producerDone.store(true, std::memory_order_release);
  });

  std::thread consumer([&] {
    for (std::size_t index = 0; index < frameCount; ++index) {
      while (ring.FillFrames() == 0U) {
        std::this_thread::yield();
      }
      StereoF32 frame{};
      if (ring.Read({&frame, 1}) != 1U ||
          frame.left != static_cast<float>(static_cast<std::int16_t>(index)) / 32768.0F ||
          frame.right != static_cast<float>(static_cast<std::int16_t>(~index)) / 32768.0F) {
        ordered.store(false, std::memory_order_relaxed);
      }
    }
  });

  producer.join();
  consumer.join();
  CHECK(producerDone.load(std::memory_order_acquire));
  CHECK(ordered.load(std::memory_order_relaxed));
  CHECK(ring.FillFrames() == 0U);
  CHECK(ring.Underruns() == 0U);
  CHECK(ring.Overruns() == 0U);
}

TEST_CASE("PCM ring retains full empty and ordering semantics across position wrap") {
  PcmRingBuffer<4> ring;
  constexpr auto nearWrap = std::numeric_limits<PcmRingBuffer<4>::Position>::max() - 3U;
  ring.SeedPositionsForTest(nearWrap, nearWrap);
  const std::array<StereoI16, 6> input{Frame(1, -1), Frame(2, -2),
                                       Frame(3, -3), Frame(4, -4),
                                       Frame(5, -5), Frame(6, -6)};

  CHECK(ring.Write({input.data(), 4}) == 4);
  CHECK(ring.FillFrames() == 4);
  CHECK(ring.Write({input.data() + 4, 1}) == 0);
  std::array<StereoF32, 2> first{};
  CHECK(ring.Read(first) == 2);
  CHECK(ring.Write({input.data() + 4, 2}) == 2);
  std::array<StereoF32, 4> rest{};
  CHECK(ring.Read(rest) == 4);
  CheckFrame(first[0], 1.0F / 32768.0F, -1.0F / 32768.0F);
  CheckFrame(first[1], 2.0F / 32768.0F, -2.0F / 32768.0F);
  for (std::size_t index = 0; index < rest.size(); ++index) {
    const float value = static_cast<float>(index + 3) / 32768.0F;
    CheckFrame(rest[index], value, -value);
  }
  CHECK(ring.FillFrames() == 0);
}

TEST_CASE("output resampler preserves identity rate across partial calls and reset") {
  OutputResampler resampler(44100, 44100);
  const std::array<StereoF32, 5> input{{{0.0F, 0.5F}, {0.1F, 0.4F},
                                        {0.2F, 0.3F}, {0.3F, 0.2F},
                                        {0.4F, 0.1F}}};
  std::array<StereoF32, 2> first{};
  auto result = resampler.Process(input, first);
  CHECK(result.inputFramesConsumed == 2);
  CHECK(result.outputFramesProduced == 2);
  CheckFrame(first[0], 0.0F, 0.5F);
  CheckFrame(first[1], 0.1F, 0.4F);

  std::array<StereoF32, 3> second{};
  result = resampler.Process({input.data() + result.inputFramesConsumed,
                              input.size() - result.inputFramesConsumed}, second);
  CHECK(result.inputFramesConsumed == 3);
  CHECK(result.outputFramesProduced == 3);
  for (std::size_t index = 0; index < second.size(); ++index) {
    CheckFrame(second[index], input[index + 2].left, input[index + 2].right);
  }

  resampler.Reset();
  std::array<StereoF32, 5> resetOutput{};
  result = resampler.Process(input, resetOutput);
  CHECK(result.inputFramesConsumed == input.size());
  CHECK(result.outputFramesProduced == input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    CheckFrame(resetOutput[index], input[index].left, input[index].right);
  }
}

TEST_CASE("output resampler preserves DC endpoints through the final flush") {
  OutputResampler resampler(4, 7);
  const std::array<StereoF32, 5> input{{{0.25F, -0.75F}, {0.25F, -0.75F},
                                        {0.25F, -0.75F}, {0.25F, -0.75F},
                                        {0.25F, -0.75F}}};
  std::array<StereoF32, 16> first{};
  const auto firstResult = resampler.Process(input, first);
  CHECK(firstResult.inputFramesConsumed == input.size());

  std::array<StereoF32, 16> tail{};
  const auto tailResult = resampler.Flush(tail);
  REQUIRE(firstResult.outputFramesProduced + tailResult.outputFramesProduced == 9);
  for (std::size_t index = 0; index < firstResult.outputFramesProduced; ++index) {
    CheckFrame(first[index], 0.25F, -0.75F);
  }
  for (std::size_t index = 0; index < tailResult.outputFramesProduced; ++index) {
    CheckFrame(tail[index], 0.25F, -0.75F);
  }
}

TEST_CASE("output resampler safely handles invalid rates and empty or one-frame streams") {
  std::array<StereoF32, 4> output{};
  const std::array<StereoF32, 1> oneFrame{{{0.25F, -0.5F}}};
  for (OutputResampler resampler : {OutputResampler(0, 48000),
                                    OutputResampler(44100, 0)}) {
    const auto processResult = resampler.Process(oneFrame, output);
    CHECK(processResult.inputFramesConsumed == 0);
    CHECK(processResult.outputFramesProduced == 0);
    CHECK(resampler.Flush(output).outputFramesProduced == 0);
  }

  OutputResampler empty(4, 7);
  CHECK(empty.Process({}, output).outputFramesProduced == 0);
  CHECK(empty.Flush(output).outputFramesProduced == 0);

  OutputResampler oneFrameResampler(4, 7);
  CHECK(oneFrameResampler.Process(oneFrame, output).inputFramesConsumed == 1);
  const auto tail = oneFrameResampler.Flush(output);
  CHECK(tail.outputFramesProduced == 2);
  CheckFrame(output[0], 0.25F, -0.5F);
  CheckFrame(output[1], 0.25F, -0.5F);
}

TEST_CASE("output resampler supports downsampling and partial non-identity output continuation") {
  const std::array<StereoF32, 4> input{{{0.0F, 0.0F}, {0.25F, -0.25F},
                                        {0.5F, -0.5F}, {0.75F, -0.75F}}};
  OutputResampler downsampler(4, 2);
  std::array<StereoF32, 4> downsampled{};
  const auto downsampleResult = downsampler.Process(input, downsampled);
  CHECK(downsampleResult.inputFramesConsumed == input.size());
  CHECK(downsampleResult.outputFramesProduced == 2);
  CHECK(downsampler.Flush({downsampled.data() + 2, 2}).outputFramesProduced == 0);
  CheckFrame(downsampled[0], 0.0F, 0.0F);
  CheckFrame(downsampled[1], 0.5F, -0.5F);

  OutputResampler upsampler(4, 7);
  std::array<StereoF32, 1> first{};
  const auto firstResult = upsampler.Process(input, first);
  CHECK(firstResult.inputFramesConsumed == 2);
  CHECK(firstResult.outputFramesProduced == 1);
  CheckFrame(first[0], 0.0F, 0.0F);

  std::array<StereoF32, 8> second{};
  const auto secondResult = upsampler.Process(
      {input.data() + firstResult.inputFramesConsumed,
       input.size() - firstResult.inputFramesConsumed}, second);
  CHECK(secondResult.inputFramesConsumed == 2);
  CHECK(secondResult.outputFramesProduced == 5);
  CheckFrame(second[0], 0.25F * (4.0F / 7.0F),
             -0.25F * (4.0F / 7.0F));
}

TEST_CASE("output resampler flushes in pieces and reset discards non-identity history") {
  const std::array<StereoF32, 5> input{{{0.0F, 0.0F}, {0.1F, -0.1F},
                                        {0.2F, -0.2F}, {0.3F, -0.3F},
                                        {0.4F, -0.4F}}};
  OutputResampler resampler(4, 7);
  std::array<StereoF32, 16> rendered{};
  const auto processResult = resampler.Process(input, rendered);
  CHECK(processResult.inputFramesConsumed == input.size());
  CHECK(processResult.outputFramesProduced == 7);
  std::array<StereoF32, 1> tailA{};
  std::array<StereoF32, 1> tailB{};
  CHECK(resampler.Flush(tailA).outputFramesProduced == 1);
  CHECK(resampler.Flush(tailB).outputFramesProduced == 1);
  CHECK(resampler.Flush(tailB).outputFramesProduced == 0);

  resampler.Reset();
  std::array<StereoF32, 16> reused{};
  const auto reusedResult = resampler.Process(input, reused);
  CHECK(reusedResult.inputFramesConsumed == input.size());
  CHECK(reusedResult.outputFramesProduced == processResult.outputFramesProduced);
  for (std::size_t index = 0; index < processResult.outputFramesProduced; ++index) {
    CheckFrame(reused[index], rendered[index].left, rendered[index].right);
  }
}

TEST_CASE("output resampler converts one second from 44.1 kHz to 48 kHz across adversarial chunks") {
  constexpr std::uint32_t sourceRate = 44100;
  constexpr std::uint32_t destinationRate = 48000;
  constexpr float frequency = 440.0F;
  constexpr float twoPi = 6.28318530717958647692F;

  std::vector<StereoF32> input(sourceRate);
  for (std::size_t index = 0; index < input.size(); ++index) {
    const float sample = std::sin(twoPi * frequency *
                                  static_cast<float>(index) / sourceRate);
    input[index] = StereoF32{sample, sample};
  }

  OutputResampler resampler(sourceRate, destinationRate);
  std::vector<StereoF32> output;
  output.reserve(destinationRate);
  constexpr std::array<std::size_t, 9> chunks{1, 37, 2, 251, 7, 3, 127, 19, 509};
  std::size_t inputOffset = 0;
  std::size_t chunkIndex = 0;
  while (inputOffset < input.size()) {
    const std::size_t requested = chunks[chunkIndex++ % chunks.size()];
    const std::size_t available = std::min(requested, input.size() - inputOffset);
    std::size_t consumed = 0;
    do {
      std::array<StereoF32, 1024> scratch{};
      const auto result = resampler.Process(
          {input.data() + inputOffset + consumed, available - consumed}, scratch);
      consumed += result.inputFramesConsumed;
      output.insert(output.end(), scratch.begin(),
                    scratch.begin() + static_cast<std::ptrdiff_t>(result.outputFramesProduced));
      const bool madeProgress = result.inputFramesConsumed != 0 ||
                                result.outputFramesProduced != 0;
      const bool didNotStall = madeProgress || consumed == available;
      REQUIRE(didNotStall);
    } while (consumed < available);
    inputOffset += consumed;
  }

  while (true) {
    std::array<StereoF32, 1024> scratch{};
    const auto result = resampler.Flush(scratch);
    output.insert(output.end(), scratch.begin(),
                  scratch.begin() + static_cast<std::ptrdiff_t>(result.outputFramesProduced));
    if (result.outputFramesProduced == 0) {
      break;
    }
  }

  REQUIRE(output.size() == destinationRate);
  float maxError = 0.0F;
  for (std::size_t index = 0; index < output.size(); ++index) {
    const float expected = std::sin(twoPi * frequency *
                                    static_cast<float>(index) / destinationRate);
    CHECK(output[index].left == doctest::Approx(output[index].right).epsilon(0.000001));
    CHECK(output[index].left >= -1.0F);
    CHECK(output[index].left <= 1.0F);
    CHECK(output[index].left == doctest::Approx(expected).epsilon(0.0012));
    maxError = std::max(maxError, std::fabs(output[index].left - expected));
  }
  CHECK(maxError < 0.0006F);
  CHECK(output.front().left == doctest::Approx(0.0F));
  CHECK(output.back().left == doctest::Approx(
            std::sin(twoPi * frequency * (destinationRate - 1) / destinationRate))
            .epsilon(0.0012));
}
