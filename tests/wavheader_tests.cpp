#include "doctest/doctest.h"

#include "Adapters/wasm/filesystem/WasmFileSystem.h"
#include "Application/Instruments/WavFile.h"
#include "Application/Instruments/WavReadPolicy.h"
#include "Application/Model/Config.h"
#include "Services/Audio/WavFileWriter.h"
#include "Services/Audio/WavHeader.h"
#include "System/FileSystem/I_File.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace {

struct ByteWriter {
  uint8_t data[256];
  size_t size = 0;

  bool AppendBytes(const void *src, size_t len) {
    if (size + len > sizeof(data)) {
      return false;
    }
    std::memcpy(data + size, src, len);
    size += len;
    return true;
  }

  bool AppendU32(uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = static_cast<uint8_t>(value & 0xFF);
    bytes[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    bytes[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    return AppendBytes(bytes, sizeof(bytes));
  }

  bool AppendU16(uint16_t value) {
    uint8_t bytes[2];
    bytes[0] = static_cast<uint8_t>(value & 0xFF);
    bytes[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    return AppendBytes(bytes, sizeof(bytes));
  }

  bool AppendFourCC(const char *fourcc) { return AppendBytes(fourcc, 4); }
};

class TestFile final : public I_File {
public:
  TestFile(const uint8_t *data, size_t size) : data_(data), size_(size) {}

  int Read(void *ptr, int size) override {
    if (size <= 0) {
      return 0;
    }
    size_t remaining = size_ - pos_;
    size_t to_read = static_cast<size_t>(size);
    if (to_read > remaining) {
      to_read = remaining;
    }
    if (to_read > 0) {
      std::memcpy(ptr, data_ + pos_, to_read);
      pos_ += to_read;
    }
    if (to_read < static_cast<size_t>(size)) {
      error_ = true;
    }
    return static_cast<int>(to_read);
  }

  int GetC() override {
    if (pos_ >= size_) {
      error_ = true;
      return -1;
    }
    return data_[pos_++];
  }

  int Write(const void *ptr, int size, int nmemb) override {
    (void)ptr;
    (void)size;
    (void)nmemb;
    error_ = true;
    return 0;
  }

  void Seek(long offset, int whence) override {
    size_t base = 0;
    if (whence == SEEK_CUR) {
      base = pos_;
    } else if (whence == SEEK_END) {
      base = size_;
    }

    long next = static_cast<long>(base) + offset;
    if (next < 0) {
      pos_ = 0;
      error_ = true;
    } else if (static_cast<size_t>(next) > size_) {
      pos_ = size_;
      error_ = true;
    } else {
      pos_ = static_cast<size_t>(next);
    }
  }

  long Tell() override { return static_cast<long>(pos_); }

  int Error() override { return error_ ? 1 : 0; }

  bool Sync() override { return true; }

  void Dispose() override {}

protected:
  bool Close() override { return true; }

private:
  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
  size_t pos_ = 0;
  bool error_ = false;
};

class HeaderWriteFile final : public I_File {
public:
  bool failWrite = false, failSync = false, failClose = false;
  int Read(void *ptr, int size) override {
    if (!ptr || size <= 0 || position_ >= size_) {
      return 0;
    }
    const size_t available = size_ - position_;
    const size_t count = std::min(static_cast<size_t>(size), available);
    std::memcpy(ptr, data_ + position_, count);
    position_ += count;
    return static_cast<int>(count);
  }

  int GetC() override {
    if (position_ >= size_) {
      return -1;
    }
    return data_[position_++];
  }

  int Write(const void *ptr, int size, int nmemb) override {
    if (failWrite)
      return nmemb > 0 ? nmemb - 1 : 0;
    if (!ptr || size <= 0 || nmemb <= 0) {
      return 0;
    }
    const size_t count = static_cast<size_t>(size) * static_cast<size_t>(nmemb);
    if (position_ + count > sizeof(data_)) {
      error_ = true;
      return 0;
    }
    std::memcpy(data_ + position_, ptr, count);
    position_ += count;
    size_ = std::max(size_, position_);
    return nmemb;
  }

  void Seek(long offset, int whence) override {
    size_t base = 0;
    if (whence == SEEK_CUR) {
      base = position_;
    } else if (whence == SEEK_END) {
      base = size_;
    }
    const long next = static_cast<long>(base) + offset;
    if (next < 0 || static_cast<size_t>(next) > sizeof(data_)) {
      error_ = true;
      return;
    }
    position_ = static_cast<size_t>(next);
  }

  long Tell() override { return static_cast<long>(position_); }
  bool Truncate(long size) override {
    if (size < 0 || static_cast<size_t>(size) > sizeof(data_)) {
      error_ = true;
      return false;
    }
    size_ = static_cast<size_t>(size);
    return true;
  }
  int Error() override { return error_ ? 1 : 0; }
  bool Sync() override { return !error_ && !failSync; }
  void Dispose() override {}

  const uint8_t *data() const { return data_; }
  size_t size() const { return size_; }

protected:
  bool Close() override { return !failClose; }

private:
  uint8_t data_[256] = {0};
  size_t size_ = 0;
  size_t position_ = 0;
  bool error_ = false;
};

uint16_t ReadU16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8U);
}

uint32_t ReadU32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) |
         (static_cast<uint32_t>(data[3]) << 24U);
}

class FileSystemInstallGuard {
public:
  explicit FileSystemInstallGuard(FileSystem &fileSystem)
      : previous_(FileSystem::GetInstance()) {
    FileSystem::Install(&fileSystem);
  }

  ~FileSystemInstallGuard() { FileSystem::Install(previous_); }

private:
  FileSystem *previous_;
};

ByteWriter BuildPcmWav(uint16_t channels, uint32_t sampleRate,
                       uint16_t bitsPerSample, uint32_t dataSize) {
  ByteWriter writer;
  uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
  uint16_t blockAlign = channels * (bitsPerSample / 8);

  writer.AppendFourCC("RIFF");
  writer.AppendU32(0); // placeholder, patched later
  writer.AppendFourCC("WAVE");

  writer.AppendFourCC("fmt ");
  writer.AppendU32(16);
  writer.AppendU16(1); // PCM
  writer.AppendU16(channels);
  writer.AppendU32(sampleRate);
  writer.AppendU32(byteRate);
  writer.AppendU16(blockAlign);
  writer.AppendU16(bitsPerSample);

  writer.AppendFourCC("data");
  writer.AppendU32(dataSize);

  if (dataSize > 0) {
    uint8_t zeros[8] = {0};
    uint32_t remaining = dataSize;
    while (remaining > 0) {
      uint32_t chunk = remaining > sizeof(zeros) ? sizeof(zeros) : remaining;
      writer.AppendBytes(zeros, chunk);
      remaining -= chunk;
    }
  }

  uint32_t riffSize = static_cast<uint32_t>(writer.size - 8);
  std::memcpy(writer.data + 4, &riffSize, sizeof(riffSize));

  return writer;
}

ByteWriter BuildPcmWavWithAncillaryChunks(uint32_t dataSize) {
  ByteWriter writer;
  writer.AppendFourCC("RIFF");
  writer.AppendU32(0);
  writer.AppendFourCC("WAVE");
  writer.AppendFourCC("JUNK");
  writer.AppendU32(4);
  writer.AppendFourCC("safe");
  writer.AppendFourCC("fmt ");
  writer.AppendU32(16);
  writer.AppendU16(1);
  writer.AppendU16(1);
  writer.AppendU32(44100);
  writer.AppendU32(88200);
  writer.AppendU16(2);
  writer.AppendU16(16);
  writer.AppendFourCC("LIST");
  writer.AppendU32(3);
  writer.AppendBytes("tag", 3);
  const uint8_t padding = 0U;
  writer.AppendBytes(&padding, 1);
  writer.AppendFourCC("data");
  writer.AppendU32(dataSize);
  uint8_t zeros[8]{};
  uint32_t remaining = dataSize;
  while (remaining != 0U) {
    const uint32_t count = std::min<uint32_t>(remaining, sizeof(zeros));
    writer.AppendBytes(zeros, count);
    remaining -= count;
  }
  const uint32_t riffSize = static_cast<uint32_t>(writer.size - 8U);
  std::memcpy(writer.data + 4U, &riffSize, sizeof(riffSize));
  return writer;
}

ByteWriter BuildExtensibleWav(uint16_t channels, uint32_t sampleRate,
                              uint16_t bitsPerSample, uint32_t dataSize,
                              uint16_t subtype) {
  ByteWriter writer;
  uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
  uint16_t blockAlign = channels * (bitsPerSample / 8);

  writer.AppendFourCC("RIFF");
  writer.AppendU32(0); // placeholder, patched later
  writer.AppendFourCC("WAVE");

  writer.AppendFourCC("fmt ");
  writer.AppendU32(40);
  writer.AppendU16(0xFFFE); // WAVE_FORMAT_EXTENSIBLE
  writer.AppendU16(channels);
  writer.AppendU32(sampleRate);
  writer.AppendU32(byteRate);
  writer.AppendU16(blockAlign);
  writer.AppendU16(bitsPerSample);
  writer.AppendU16(22);                        // cbSize
  writer.AppendU16(bitsPerSample);             // valid bits per sample
  writer.AppendU32(channels == 1 ? 0x4 : 0x3); // mono center / stereo L|R
  writer.AppendU16(subtype); // KSDATAFORMAT_SUBTYPE_* low 16 bits of Data1
  writer.AppendU16(0);
  writer.AppendU16(0);
  writer.AppendU16(0x0010);
  writer.AppendBytes("\x80\x00\x00\xAA\x00\x38\x9B\x71", 8);

  writer.AppendFourCC("data");
  writer.AppendU32(dataSize);

  if (dataSize > 0) {
    uint8_t zeros[8] = {0};
    uint32_t remaining = dataSize;
    while (remaining > 0) {
      uint32_t chunk = remaining > sizeof(zeros) ? sizeof(zeros) : remaining;
      writer.AppendBytes(zeros, chunk);
      remaining -= chunk;
    }
  }

  uint32_t riffSize = static_cast<uint32_t>(writer.size - 8);
  std::memcpy(writer.data + 4, &riffSize, sizeof(riffSize));

  return writer;
}

} // namespace

TEST_CASE("WriteHeader defaults to 16-bit stereo PCM") {
  HeaderWriteFile file;

  REQUIRE(WavHeaderWriter::WriteHeader(&file));
  REQUIRE(file.size() == 44U);
  CHECK(ReadU16(file.data() + 22U) == 2U);
  CHECK(ReadU32(file.data() + 24U) == 44100U);
  CHECK(ReadU32(file.data() + 28U) == 176400U);
  CHECK(ReadU16(file.data() + 32U) == 4U);
  CHECK(ReadU16(file.data() + 34U) == 16U);
}

TEST_CASE("UpdateFileSize finalizes the canonical placeholder header") {
  HeaderWriteFile file;
  REQUIRE(WavHeaderWriter::WriteHeader(&file, 44100U, 1U, 16U));
  const uint8_t samples[4]{};
  REQUIRE(file.Write(samples, 1, sizeof(samples)) == sizeof(samples));

  REQUIRE(WavHeaderWriter::UpdateFileSize(&file, 2U, 1U, 2U));
  CHECK(file.size() == 48U);
  CHECK(ReadU32(file.data() + 4U) == 40U);
  CHECK(ReadU32(file.data() + 40U) == 4U);
}

TEST_CASE("UpdateFileSize pads an odd mono 8-bit data chunk") {
  HeaderWriteFile file;
  REQUIRE(WavHeaderWriter::WriteHeader(&file, 44100U, 1U, 8U));
  const uint8_t sample = 0x80U;
  REQUIRE(file.Write(&sample, 1, 1) == 1);

  REQUIRE(WavHeaderWriter::UpdateFileSize(&file, 1U, 1U, 1U));
  CHECK(file.size() == 46U);
  CHECK(ReadU32(file.data() + 4U) == 38U);
  CHECK(ReadU32(file.data() + 40U) == 1U);
  CHECK(file.data()[45U] == 0U);
  const auto finalized = WavHeaderWriter::ReadHeader(&file);
  REQUIRE(finalized.has_value());
  CHECK(finalized->dataChunkSize == 1U);
}

TEST_CASE("UpdateFileSize patches the parsed data chunk and truncates") {
  Config::SetImportResampler(0);
  const auto verify = [](const ByteWriter &wav) {
    TestFile source(wav.data, wav.size);
    const auto parsed = WavHeaderWriter::ReadHeader(&source);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->dataOffset > 44U);

    HeaderWriteFile file;
    REQUIRE(file.Write(wav.data, 1, static_cast<int>(wav.size)) ==
            static_cast<int>(wav.size));
    const uint32_t byte40Before = ReadU32(file.data() + 40U);
    const uint32_t finalSize = parsed->dataOffset + 4U;
    file.Seek(static_cast<long>(finalSize), SEEK_SET);

    REQUIRE(WavHeaderWriter::UpdateFileSize(&file, 2U, 1U, 2U));
    CHECK(file.size() == finalSize);
    CHECK(ReadU32(file.data() + 4U) == finalSize - 8U);
    CHECK(ReadU32(file.data() + parsed->dataOffset - 4U) == 4U);
    CHECK(ReadU32(file.data() + 40U) == byte40Before);

    const auto finalized = WavHeaderWriter::ReadHeader(&file);
    REQUIRE(finalized.has_value());
    CHECK(finalized->dataOffset == parsed->dataOffset);
    CHECK(finalized->dataChunkSize == 4U);
  };

  verify(BuildPcmWavWithAncillaryChunks(8U));
  verify(BuildExtensibleWav(1U, 44100U, 16U, 8U, 1U));
}

TEST_CASE("WavFileWriter finalizes a WAV through a real stdio stream") {
  const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     "picotracker-wav-writer-finalization-test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  REQUIRE(std::filesystem::create_directories(root, error));

  WasmFileSystem fileSystem(root.string());
  FileSystemInstallGuard install(fileSystem);
  fixed samples[4]{};

  {
    WavFileWriter writer;
    REQUIRE(writer.Open("/round-trip.wav"));
    writer.AddBuffer(samples, 2);
    writer.Close();
  }

  auto file = fileSystem.Open("/round-trip.wav", "rb");
  REQUIRE(file);
  const auto header = WavHeaderWriter::ReadHeader(file.get());
  REQUIRE(header.has_value());
  CHECK(header->dataChunkSize == 8U);
  CHECK(std::filesystem::file_size(root / "round-trip.wav", error) == 52U);
  CHECK_FALSE(error);

  file.reset();
  std::filesystem::remove_all(root, error);
}

TEST_CASE("WavFile zero-pads GetBuffer past logical end of file") {
  Config::SetImportResampler(0);
  const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     "picotracker-wav-get-buffer-test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  REQUIRE(std::filesystem::create_directories(root, error));

  ByteWriter poison = BuildPcmWav(1, 44100, 16, 8);
  const int16_t poisonSamples[4] = {1111, 2222, 3333, 4444};
  std::memcpy(poison.data + 44, poisonSamples, sizeof(poisonSamples));
  ByteWriter shortFile = BuildPcmWav(1, 44100, 16, 4);
  const int16_t shortSamples[2] = {1234, 5678};
  std::memcpy(shortFile.data + 44, shortSamples, sizeof(shortSamples));

  {
    std::ofstream output(root / "poison.wav", std::ios::binary);
    output.write(reinterpret_cast<const char *>(poison.data), poison.size);
    REQUIRE(output.good());
  }
  {
    std::ofstream output(root / "short.wav", std::ios::binary);
    output.write(reinterpret_cast<const char *>(shortFile.data),
                 shortFile.size);
    REQUIRE(output.good());
  }

  WasmFileSystem fileSystem(root.string());
  FileSystemInstallGuard install(fileSystem);
  WavFile wave;

  REQUIRE(wave.Open("/poison.wav").has_value());
  REQUIRE(wave.GetBuffer(0, 4));
  wave.Close();

  REQUIRE(wave.Open("/short.wav").has_value());
  REQUIRE(wave.GetBuffer(0, 4));
  const auto *samples = static_cast<const int16_t *>(wave.GetSampleBuffer(-1));
  REQUIRE(samples != nullptr);
  CHECK(samples[0] == shortSamples[0]);
  CHECK(samples[1] == shortSamples[1]);
  CHECK(samples[2] == 0);
  CHECK(samples[3] == 0);

  wave.Close();
  std::filesystem::remove_all(root, error);
}

TEST_CASE("WavFile reads high-depth audio into one-frame PCM16 buffers") {
  const auto root = std::filesystem::temp_directory_path() /
                    "picotracker-wav-stream-conversion-test";
  std::filesystem::create_directories(root);
  WasmFileSystem fileSystem(root.string());
  FileSystemInstallGuard install(fileSystem);
  Config::SetImportResampler(0);
  for (const auto format : {1, 3}) {
    for (const auto bits : {8, 16, 24, 32, 64}) {
      if ((format == 1 && bits == 64) || (format == 3 && bits < 32))
        continue;
      for (const auto channels : {1, 2}) {
        CAPTURE(format);
        CAPTURE(bits);
        CAPTURE(channels);
        auto wav = BuildPcmWav(channels, 44100, bits, 2 * channels * bits / 8);
        wav.data[20] = format;
        for (int sample = 0; sample < 2 * channels; ++sample) {
          const int value = sample % 2 == 0 ? 8192 : -16384;
          auto *destination = wav.data + 44 + sample * bits / 8;
          if (format == 3) {
            const double value64 = value / 32768.0;
            const float value32 = static_cast<float>(value64);
            if (bits == 64)
              std::memcpy(destination, &value64, sizeof(value64));
            else
              std::memcpy(destination, &value32, sizeof(value32));
          } else {
            const int32_t pcm =
                bits == 8 ? 128 + value / 256 : value * (1 << (bits - 16));
            for (int byte = 0; byte < bits / 8; ++byte)
              destination[byte] = static_cast<uint32_t>(pcm) >> (byte * 8);
          }
        }
        {
          std::ofstream file(root / "input.wav", std::ios::binary);
          file.write(reinterpret_cast<const char *>(wav.data), wav.size);
          REQUIRE(file.good());
        }
        WavFile wave;
        REQUIRE(wave.Open("/input.wav").has_value());
        // One leading and trailing canary around exactly one stereo frame.
        int16_t output[4] = {123, 123, 123, 123};
        for (int frame = 0; frame < 2; ++frame) {
          uint32_t bytesRead = 999;
          REQUIRE(
              wave.Read(output + 1, channels * sizeof(int16_t), &bytesRead));
          REQUIRE(bytesRead == channels * sizeof(int16_t));
          for (int channel = 0; channel < channels; ++channel)
            CHECK(output[channel + 1] ==
                  ((frame * channels + channel) % 2 == 0 ? 8192 : -16384));
          CHECK(output[0] == 123);
          CHECK(output[channels + 1] == 123);
        }
        uint32_t bytesRead = 999;
        REQUIRE(wave.Read(output + 1, channels * sizeof(int16_t), &bytesRead));
        CHECK(bytesRead == 0);
        REQUIRE(wave.Rewind());
        float floats[4]{};
        uint32_t samplesRead = 999;
        REQUIRE(wave.ReadFloat(floats, 2 * channels, &samplesRead));
        REQUIRE(samplesRead == 2 * channels);
        for (uint32_t sample = 0; sample < samplesRead; ++sample)
          CHECK(floats[sample] == (sample % 2 == 0 ? 0.25F : -0.5F));
        wave.Close();
        CHECK_FALSE(wave.Rewind());
        CHECK_FALSE(
            wave.Read(output + 1, channels * sizeof(int16_t), &bytesRead));
        CHECK(bytesRead == 0);
        CHECK_FALSE(wave.ReadFloat(floats, 2 * channels, &samplesRead));
        CHECK(samplesRead == 0);
      }
    }
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("WavFile handles non-finite floating-point sample data") {
  const auto root =
      std::filesystem::temp_directory_path() / "picotracker-wav-nonfinite-test";
  std::filesystem::create_directories(root);
  WasmFileSystem fileSystem(root.string());
  FileSystemInstallGuard install(fileSystem);
  Config::SetImportResampler(0);
  auto wav = BuildPcmWav(1, 44100, 64, 4 * sizeof(double));
  wav.data[20] = 3;
  const double values[] = {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(), 0.25};
  std::memcpy(wav.data + 44, values, sizeof(values));
  {
    std::ofstream file(root / "input.wav", std::ios::binary);
    file.write(reinterpret_cast<const char *>(wav.data), wav.size);
    REQUIRE(file.good());
  }
  WavFile wave;
  REQUIRE(wave.Open("/input.wav").has_value());
  int16_t pcm[4]{};
  uint32_t bytesRead = 999;
  REQUIRE(wave.Read(pcm, sizeof(pcm), &bytesRead));
  REQUIRE(bytesRead == sizeof(pcm));
  CHECK(pcm[0] == 0);
  CHECK(pcm[1] == 32767);
  CHECK(pcm[2] == -32768);
  CHECK(pcm[3] == 8192);
  REQUIRE(wave.Rewind());
  float floats[4]{};
  uint32_t samplesRead = 999;
  REQUIRE(wave.ReadFloat(floats, 4, &samplesRead));
  REQUIRE(samplesRead == 4);
  CHECK(floats[0] == 0);
  CHECK(floats[1] == 0);
  CHECK(floats[2] == 0);
  CHECK(floats[3] == 0.25F);
  wave.Close();
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "WavFile streams multiple conversion blocks and reports premature EOF") {
  const auto root = std::filesystem::temp_directory_path() /
                    "picotracker-wav-block-stream-test";
  std::filesystem::create_directories(root);
  WasmFileSystem fileSystem(root.string());
  FileSystemInstallGuard install(fileSystem);
  Config::SetImportResampler(0);
  constexpr uint32_t frames = 32768;
  auto header = BuildPcmWav(1, 44100, 16, 2);
  const uint32_t dataBytes = frames * 2;
  const uint32_t riffBytes = dataBytes + 36;
  std::memcpy(header.data + 4, &riffBytes, sizeof(riffBytes));
  std::memcpy(header.data + 40, &dataBytes, sizeof(dataBytes));
  const std::vector<int16_t> source(frames, 8192);
  {
    std::ofstream file(root / "input.wav", std::ios::binary);
    file.write(reinterpret_cast<const char *>(header.data), 44);
    file.write(reinterpret_cast<const char *>(source.data()), dataBytes);
    REQUIRE(file.good());
  }
  WavFile wave;
  REQUIRE(wave.Open("/input.wav").has_value());
  std::vector<int16_t> pcm(frames);
  uint32_t bytesRead = 0;
  REQUIRE(wave.Read(pcm.data(), dataBytes, &bytesRead));
  CHECK(bytesRead == dataBytes);
  CHECK(pcm == source);
  REQUIRE(wave.Rewind());
  std::vector<float> floats(frames);
  uint32_t samplesRead = 0;
  REQUIRE(wave.ReadFloat(floats.data(), frames, &samplesRead));
  CHECK(samplesRead == frames);
  CHECK(std::all_of(floats.begin(), floats.end(),
                    [](float value) { return value == 0.25F; }));
  REQUIRE(wave.Rewind());
  // Simulate the backing file being truncated after its valid header was read.
  // The request exceeds stdio read-ahead, so it must reach the premature EOF.
  std::filesystem::resize_file(root / "input.wav", 44);
  CHECK_FALSE(wave.Read(pcm.data(), dataBytes, &bytesRead));
  CHECK(bytesRead < dataBytes);
  wave.Close();
  std::filesystem::remove_all(root);
}

TEST_CASE("ReadHeader parses valid PCM WAV") {
  Config::SetImportResampler(0);
  ByteWriter wav = BuildPcmWav(2, 44100, 16, 4);
  TestFile file(wav.data, wav.size);

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE(result.has_value());

  CHECK(result->audioFormat == 1);
  CHECK(result->numChannels == 2);
  CHECK(result->sampleRate == 44100);
  CHECK(result->bitsPerSample == 16);
  CHECK(result->dataChunkSize == 4);
  CHECK(result->dataOffset > 0);
}

TEST_CASE("ReadHeader rejects truncated extensible payload declaration") {
  Config::SetImportResampler(0);
  ByteWriter wav = BuildExtensibleWav(2, 44100, 16, 4, 1);
  wav.data[36] = 21;
  wav.data[37] = 0;
  TestFile file(wav.data, wav.size);

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == INVALID_HEADER);
}

TEST_CASE("ReadHeader rejects missing RIFF") {
  ByteWriter wav = BuildPcmWav(2, 44100, 16, 4);
  wav.data[0] = 'N';
  TestFile file(wav.data, wav.size);

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == UNSUPPORTED_FILE_FORMAT);
}

TEST_CASE("ReadHeader rejects fmt chunk too small") {
  ByteWriter wav = BuildPcmWav(2, 44100, 16, 4);
  uint32_t fmtSize = 12;
  std::memcpy(wav.data + 16, &fmtSize, sizeof(fmtSize));
  TestFile file(wav.data, wav.size);

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == INVALID_HEADER);
}

TEST_CASE("ReadHeader rejects unsupported sample rate without resampling") {
  Config::SetImportResampler(0);
  ByteWriter wav = BuildPcmWav(2, 48000, 16, 4);
  TestFile file(wav.data, wav.size);

  auto result = ReadTrackerWavHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == UNSUPPORTED_SAMPLERATE);
  REQUIRE(WavHeaderWriter::ReadHeader(
      &file)); // Format inspection is independent of Config.
}

TEST_CASE("ReadHeader rejects unsupported audio format") {
  ByteWriter wav = BuildPcmWav(2, 44100, 16, 4);
  uint16_t format = 2;
  std::memcpy(wav.data + 20, &format, sizeof(format));
  TestFile file(wav.data, wav.size);

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == UNSUPPORTED_AUDIO_FORMAT);
}

TEST_CASE("ReadHeader rejects inconsistent PCM block alignment") {
  Config::SetImportResampler(0);
  ByteWriter wav = BuildPcmWav(2, 44100, 24, 12);
  const uint16_t invalidBlockAlign = 1;
  std::memcpy(wav.data + 32, &invalidBlockAlign, sizeof(invalidBlockAlign));
  TestFile file(wav.data, wav.size);

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == INVALID_HEADER);
}

TEST_CASE("ReadHeader rejects block alignment products wider than the field") {
  Config::SetImportResampler(0);
  ByteWriter wav =
      BuildPcmWav(std::numeric_limits<uint16_t>::max(), 44100, 32, 4);
  TestFile file(wav.data, wav.size);

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == INVALID_HEADER);
}

TEST_CASE("ReadHeader parses extensible PCM WAV") {
  Config::SetImportResampler(1);
  ByteWriter wav = BuildExtensibleWav(2, 48000, 24, 12, 1);
  TestFile file(wav.data, wav.size);

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE(result.has_value());

  CHECK(result->audioFormat == 1);
  CHECK(result->numChannels == 2);
  CHECK(result->sampleRate == 48000);
  CHECK(result->bitsPerSample == 24);
  CHECK(result->bytesPerSample == 3);
  CHECK(result->blockAlign == 6);
  CHECK(result->dataChunkSize == 12);
  CHECK(result->dataOffset > 0);
}

TEST_CASE("ReadHeader parses extensible float WAV") {
  Config::SetImportResampler(1);
  ByteWriter wav = BuildExtensibleWav(2, 48000, 32, 16, 3);
  TestFile file(wav.data, wav.size);

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE(result.has_value());

  CHECK(result->audioFormat == 3);
  CHECK(result->numChannels == 2);
  CHECK(result->sampleRate == 48000);
  CHECK(result->bitsPerSample == 32);
  CHECK(result->bytesPerSample == 4);
  CHECK(result->dataChunkSize == 16);
  CHECK(result->dataOffset > 0);
}

TEST_CASE("ReadHeader rejects unsupported extensible subtype") {
  Config::SetImportResampler(0);
  ByteWriter wav = BuildExtensibleWav(2, 44100, 16, 4, 2);
  TestFile file(wav.data, wav.size);

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == UNSUPPORTED_AUDIO_FORMAT);
}

TEST_CASE("ReadHeader rejects nonstandard extensible subtype GUID") {
  Config::SetImportResampler(0);
  ByteWriter wav = BuildExtensibleWav(2, 44100, 16, 4, 1);
  wav.data[52] ^= 1; // Corrupt the GUID tail while retaining PCM's Data1.
  TestFile file(wav.data, wav.size);

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == UNSUPPORTED_AUDIO_FORMAT);
}

TEST_CASE("ReadHeader accepts data chunk beyond RIFF when still within EOF") {
  Config::SetImportResampler(0);
  ByteWriter wav = BuildPcmWav(2, 44100, 16, 8);

  // Make RIFF chunk size too small by 4 bytes so data end exceeds RIFF bounds.
  uint32_t riffSize = 0;
  std::memcpy(&riffSize, wav.data + 4, sizeof(riffSize));
  riffSize -= 4;
  std::memcpy(wav.data + 4, &riffSize, sizeof(riffSize));

  TestFile file(wav.data, wav.size);
  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE(result.has_value());
  CHECK(result->dataChunkSize == 8);
}

TEST_CASE("ReadHeader rejects data chunk beyond EOF") {
  Config::SetImportResampler(0);
  ByteWriter wav = BuildPcmWav(2, 44100, 16, 8);

  // Increase data size field beyond available bytes in file.
  uint32_t oversizedData = 12;
  std::memcpy(wav.data + 40, &oversizedData, sizeof(oversizedData));

  TestFile file(wav.data, wav.size);
  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == INVALID_HEADER);
}

TEST_CASE("ReadHeader rejects data chunk size whose padding wraps") {
  Config::SetImportResampler(0);
  ByteWriter wav = BuildPcmWav(2, 44100, 16, 4);

  const uint32_t oversizedData = std::numeric_limits<uint32_t>::max();
  std::memcpy(wav.data + 40, &oversizedData, sizeof(oversizedData));

  TestFile file(wav.data, wav.size);
  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == INVALID_HEADER);
}

TEST_CASE("ReadHeader rejects data chunk size whose end wraps") {
  Config::SetImportResampler(0);
  ByteWriter wav = BuildPcmWav(2, 44100, 16, 4);

  const uint32_t oversizedData = std::numeric_limits<uint32_t>::max() - 1;
  std::memcpy(wav.data + 40, &oversizedData, sizeof(oversizedData));

  TestFile file(wav.data, wav.size);
  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == INVALID_HEADER);
}

TEST_CASE("WAV export retains write, sync, header and close failures") {
  for (int fault = 0; fault < 4; ++fault) {
    CAPTURE(fault);
    HeaderWriteFile file;
    WavFileWriter writer;
    REQUIRE(writer.Open(FileHandle(&file)));
    const fixed samples[] = {i2fp(1234), i2fp(-2345)};
    if (fault == 0)
      file.failWrite = true;
    CHECK(writer.AddBuffer(samples, 1) == (fault != 0));
    if (fault == 1)
      file.failSync = true;
    if (fault == 2)
      file.failWrite = true;
    if (fault == 3)
      file.failClose = true;
    CHECK_FALSE(writer.Close());
    CHECK(writer.Failed());
    CHECK_FALSE(writer.IsOpen());
    CHECK_FALSE(writer.Close());
    file.failWrite = file.failSync = file.failClose = false;
    CHECK(writer.Open(FileHandle(&file)));
    CHECK_FALSE(writer.Failed());
  }
}
