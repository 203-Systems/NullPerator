#include "doctest/doctest.h"

#include "Application/Instruments/WavHeader.h"
#include "Application/Instruments/WavFile.h"
#include "Application/Model/Config.h"
#include "Adapters/wasm/filesystem/WasmFileSystem.h"
#include "System/FileSystem/I_File.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

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
  int Read(void *ptr, int size) override {
    if (!ptr || size <= 0 || position_ >= size_) {
      return 0;
    }
    const size_t available = size_ - position_;
    const size_t count =
        std::min(static_cast<size_t>(size), available);
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
    if (!ptr || size <= 0 || nmemb <= 0) {
      return 0;
    }
    const size_t count =
        static_cast<size_t>(size) * static_cast<size_t>(nmemb);
    if (position_ + count > sizeof(data_)) {
      error_ = true;
      return 0;
    }
    std::memcpy(data_ + position_, ptr, count);
    position_ += count;
    size_ = std::max(size_, position_);
    return static_cast<int>(count);
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
  bool Sync() override { return !error_; }
  void Dispose() override {}

  const uint8_t *data() const { return data_; }
  size_t size() const { return size_; }

protected:
  bool Close() override { return true; }

private:
  uint8_t data_[256] = {0};
  size_t size_ = 0;
  size_t position_ = 0;
  bool error_ = false;
};

uint16_t ReadU16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(data[1] << 8U);
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

TEST_CASE("WavFile zero-pads GetBuffer past logical end of file") {
  Config::SetImportResampler(0);
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
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

  auto result = WavHeaderWriter::ReadHeader(&file);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == UNSUPPORTED_SAMPLERATE);
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
