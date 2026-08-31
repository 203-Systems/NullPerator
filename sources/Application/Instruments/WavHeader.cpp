/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "WavHeader.h"
#include "Application/Model/Config.h"
#include "Externals/SRC/common.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/I_File.h"

#include <cstring>
#include <limits>

namespace {

constexpr uint16_t WAV_FORMAT_PCM = 0x0001;
constexpr uint16_t WAV_FORMAT_IEEE_FLOAT = 0x0003;
constexpr uint16_t WAV_FORMAT_EXTENSIBLE = 0xFFFE;
constexpr uint8_t WAV_SUBFORMAT_PCM[16] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                           0x10, 0x00, 0x80, 0x00, 0x00, 0xAA,
                                           0x00, 0x38, 0x9B, 0x71};
constexpr uint8_t WAV_SUBFORMAT_IEEE_FLOAT[16] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

bool ReadOffset(I_File *file, uint64_t &offset) {
  const long position = file->Tell();
  if (position < 0) {
    return false;
  }
  offset = static_cast<uint64_t>(position);
  return true;
}

bool SeekTo(I_File *file, uint64_t offset) {
  if (offset > static_cast<uint64_t>(std::numeric_limits<long>::max())) {
    return false;
  }

  file->Seek(static_cast<long>(offset), SEEK_SET);
  uint64_t position = 0;
  return ReadOffset(file, position) && position == offset;
}

uint64_t PaddedChunkEnd(uint64_t dataStart, uint32_t chunkSize) {
  return dataStart + static_cast<uint64_t>(chunkSize) + (chunkSize & 1U);
}

} // namespace

bool WavHeaderWriter::WriteHeader(I_File *file, uint32_t sampleRate,
                                  uint16_t channels, uint16_t bitsPerSample) {
  if (!file)
    return false;

  // RIFF chunk
  uint32_t chunk = 0x46464952; // "RIFF"
  if (file->Write(&chunk, 1, 4) != 4)
    return false;

  uint32_t size = 0; // Placeholder, to be filled later
  if (file->Write(&size, 1, 4) != 4)
    return false;

  // WAVE chunk
  chunk = 0x45564157; // "WAVE"
  if (file->Write(&chunk, 1, 4) != 4)
    return false;

  // "fmt " subchunk
  chunk = 0x20746D66; // "fmt "
  if (file->Write(&chunk, 1, 4) != 4)
    return false;

  size = 16; // fmt chunk size for PCM
  if (file->Write(&size, 1, 4) != 4)
    return false;

  uint16_t ushort = 1; // PCM compression
  if (file->Write(&ushort, 1, 2) != 2)
    return false;

  ushort = channels; // number of channels
  if (file->Write(&ushort, 1, 2) != 2)
    return false;

  if (file->Write(&sampleRate, 1, 4) != 4)
    return false;

  uint32_t byteRate = (bitsPerSample / 8) * channels * sampleRate;
  if (file->Write(&byteRate, 1, 4) != 4)
    return false;

  ushort = (bitsPerSample / 8) * channels; // block align
  if (file->Write(&ushort, 1, 2) != 2)
    return false;

  ushort = bitsPerSample; // bits per sample
  if (file->Write(&ushort, 1, 2) != 2)
    return false;

  // data subchunk
  chunk = 0x61746164; // "data"
  if (file->Write(&chunk, 1, 4) != 4)
    return false;

  size = 0; // Placeholder, to be updated later
  if (file->Write(&size, 1, 4) != 4)
    return false;

  return file->Sync();
}

etl::expected<WavHeaderInfo, WAVEFILE_ERROR>
WavHeaderWriter::ReadHeader(I_File *file) {
  if (!file) {
    return etl::unexpected(INVALID_FILE);
  }

  file->Seek(0, SEEK_SET);

  WavHeaderInfo info;

  uint32_t chunk = 0;
  if (file->Read(&chunk, 4) != 4) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (chunk != 0x46464952) { // "RIFF"
    Trace::Error("WavHeaderWriter: Missing RIFF identifier");
    return etl::unexpected(UNSUPPORTED_FILE_FORMAT);
  }

  if (file->Read(&info.riffChunkSize, 4) != 4) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (file->Read(&chunk, 4) != 4) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (chunk != 0x45564157) { // "WAVE"
    Trace::Error("WavHeaderWriter: Missing WAVE identifier");
    return etl::unexpected(UNSUPPORTED_WAV_FORMAT);
  }

  const long afterWavePos = file->Tell();
  if (afterWavePos < 0) {
    return etl::unexpected(INVALID_HEADER);
  }
  file->Seek(0, SEEK_END);
  uint64_t fileEnd = 0;
  if (!ReadOffset(file, fileEnd) || fileEnd == 0) {
    return etl::unexpected(INVALID_HEADER);
  }
  file->Seek(afterWavePos, SEEK_SET);
  uint64_t restoredOffset = 0;
  if (!ReadOffset(file, restoredOffset) ||
      restoredOffset != static_cast<uint64_t>(afterWavePos)) {
    return etl::unexpected(INVALID_HEADER);
  }

  const uint64_t riffEnd = static_cast<uint64_t>(info.riffChunkSize) + 8U;
  if (riffEnd < static_cast<uint64_t>(afterWavePos) || riffEnd > fileEnd) {
    Trace::Error("WavHeaderWriter: Invalid RIFF chunk size");
    return etl::unexpected(INVALID_HEADER);
  }

  bool fmtFound = false;
  uint64_t fmtNextOffset = 0;

  while (!fmtFound) {
    if (file->Read(&chunk, 4) != 4) {
      return etl::unexpected(INVALID_HEADER);
    }

    uint32_t chunkSize = 0;
    if (file->Read(&chunkSize, 4) != 4) {
      return etl::unexpected(INVALID_HEADER);
    }
    info.fmtChunkSize = chunkSize;

    uint64_t chunkDataOffset = 0;
    if (!ReadOffset(file, chunkDataOffset)) {
      return etl::unexpected(INVALID_HEADER);
    }
    const uint64_t nextOffset =
        PaddedChunkEnd(chunkDataOffset, info.fmtChunkSize);
    if (nextOffset > riffEnd || nextOffset > fileEnd) {
      Trace::Error("WavHeaderWriter: fmt chunk exceeds RIFF bounds");
      return etl::unexpected(INVALID_HEADER);
    }

    if (chunk == 0x20746D66) { // "fmt "
      fmtFound = true;
      fmtNextOffset = nextOffset;
      break;
    }

    if (!SeekTo(file, nextOffset)) {
      return etl::unexpected(INVALID_HEADER);
    }
  }

  if (!fmtFound) {
    Trace::Error("WavHeaderWriter: fmt chunk missing");
    return etl::unexpected(INVALID_HEADER);
  }

  if (info.fmtChunkSize < 16) {
    Trace::Error("WavHeaderWriter: fmt chunk too small");
    return etl::unexpected(INVALID_HEADER);
  }

  if (file->Read(&info.audioFormat, 2) != 2) {
    return etl::unexpected(INVALID_HEADER);
  }

  const bool isExtensible = info.audioFormat == WAV_FORMAT_EXTENSIBLE;
  if (info.audioFormat != WAV_FORMAT_PCM &&
      info.audioFormat != WAV_FORMAT_IEEE_FLOAT && !isExtensible) {
    Trace::Error("WavHeaderWriter: Unsupported audio format %u",
                 info.audioFormat);
    return etl::unexpected(UNSUPPORTED_AUDIO_FORMAT);
  }

  if (file->Read(&info.numChannels, 2) != 2) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (info.numChannels == 0) {
    Trace::Error("WavHeaderWriter: Invalid channel count 0");
    return etl::unexpected(INVALID_HEADER);
  }

  if (file->Read(&info.sampleRate, 4) != 4) {
    return etl::unexpected(INVALID_HEADER);
  }

  bool enableResampling = Config::GetInstance()->GetValue("IMPORTRESAMP") > 0;
  if ((!enableResampling && info.sampleRate > 44100) ||
      (info.sampleRate < 44100 / SRC_MAX_RATIO) ||
      (info.sampleRate > 44100 * SRC_MAX_RATIO)) {
    Trace::Error("WavHeaderWriter: Unsupported sample rate %u",
                 info.sampleRate);
    return etl::unexpected(UNSUPPORTED_SAMPLERATE);
  }

  if (file->Read(&info.byteRate, 4) != 4) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (file->Read(&info.blockAlign, 2) != 2) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (file->Read(&info.bitsPerSample, 2) != 2) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (isExtensible) {
    if (info.fmtChunkSize < 40) {
      Trace::Error("WavHeaderWriter: Extensible fmt chunk too small (%u)",
                   info.fmtChunkSize);
      return etl::unexpected(INVALID_HEADER);
    }

    uint16_t extensionSize = 0;
    uint16_t validBitsPerSample = 0;
    uint32_t channelMask = 0;
    uint8_t subFormat[16] = {0};

    if (file->Read(&extensionSize, 2) != 2 ||
        file->Read(&validBitsPerSample, 2) != 2 ||
        file->Read(&channelMask, 4) != 4 || file->Read(subFormat, 16) != 16) {
      return etl::unexpected(INVALID_HEADER);
    }

    (void)extensionSize;
    (void)validBitsPerSample;
    (void)channelMask;

    if (std::memcmp(subFormat, WAV_SUBFORMAT_PCM, sizeof(subFormat)) == 0) {
      info.audioFormat = WAV_FORMAT_PCM;
    } else if (std::memcmp(subFormat, WAV_SUBFORMAT_IEEE_FLOAT,
                           sizeof(subFormat)) == 0) {
      info.audioFormat = WAV_FORMAT_IEEE_FLOAT;
    } else {
      Trace::Error("WavHeaderWriter: Unsupported extensible audio subtype");
      return etl::unexpected(UNSUPPORTED_AUDIO_FORMAT);
    }
  }

  const bool isPcm = info.audioFormat == WAV_FORMAT_PCM;
  const bool isFloat = info.audioFormat == WAV_FORMAT_IEEE_FLOAT;

  if (isPcm) {
    if ((info.bitsPerSample != 8) && (info.bitsPerSample != 16) &&
        (info.bitsPerSample != 24) && (info.bitsPerSample != 32)) {
      Trace::Error("WavHeaderWriter: Unsupported PCM bit depth %u",
                   info.bitsPerSample);
      return etl::unexpected(UNSUPPORTED_BITDEPTH);
    }
  } else if (isFloat) {
    if ((info.bitsPerSample != 32) && (info.bitsPerSample != 64)) {
      Trace::Error("WavHeaderWriter: Unsupported IEEE float bit depth %u",
                   info.bitsPerSample);
      return etl::unexpected(UNSUPPORTED_BITDEPTH);
    }
  }

  info.bytesPerSample = info.bitsPerSample / 8;
  const uint32_t expectedBlockAlign =
      static_cast<uint32_t>(info.numChannels) * info.bytesPerSample;
  if (expectedBlockAlign > std::numeric_limits<uint16_t>::max() ||
      info.blockAlign != expectedBlockAlign) {
    Trace::Error("WavHeaderWriter: Invalid block alignment %u (expected %u)",
                 info.blockAlign, expectedBlockAlign);
    return etl::unexpected(INVALID_HEADER);
  }

  if (!SeekTo(file, fmtNextOffset)) {
    return etl::unexpected(INVALID_HEADER);
  }

  while (true) {
    if (file->Read(&chunk, 4) != 4) {
      return etl::unexpected(INVALID_HEADER);
    }

    uint32_t chunkSize = 0;
    if (file->Read(&chunkSize, 4) != 4) {
      return etl::unexpected(INVALID_HEADER);
    }

    uint64_t dataStart = 0;
    if (!ReadOffset(file, dataStart)) {
      return etl::unexpected(INVALID_HEADER);
    }
    const uint64_t chunkEnd = PaddedChunkEnd(dataStart, chunkSize);
    if (chunkEnd > riffEnd) {
      // Some exporters write a too-small RIFF size while keeping a valid data
      // chunk that ends at/before EOF. Accept only this narrow mismatch.
      if (chunk == 0x61746164 && chunkEnd <= fileEnd) { // "data"
        Trace::Log("WAVHEADER",
                   "Accepting data chunk beyond RIFF bounds (riffEnd=%llu, "
                   "chunkEnd=%llu, fileEnd=%llu)",
                   static_cast<unsigned long long>(riffEnd),
                   static_cast<unsigned long long>(chunkEnd),
                   static_cast<unsigned long long>(fileEnd));
      } else {
        Trace::Error("WavHeaderWriter: data chunk exceeds RIFF bounds");
        return etl::unexpected(INVALID_HEADER);
      }
    }

    if (chunk == 0x61746164) { // "data"
      if (dataStart > std::numeric_limits<uint32_t>::max()) {
        return etl::unexpected(INVALID_HEADER);
      }
      info.dataChunkSize = chunkSize;
      info.dataOffset = static_cast<uint32_t>(dataStart);
      break;
    }

    if (!SeekTo(file, chunkEnd)) {
      return etl::unexpected(INVALID_HEADER);
    }

    if (chunkEnd >= riffEnd) {
      Trace::Error("WavHeaderWriter: data chunk not found within RIFF bounds");
      return etl::unexpected(INVALID_HEADER);
    }
  }

  if (info.dataChunkSize == 0) {
    Trace::Error("WavHeaderWriter: Missing or empty data chunk");
    return etl::unexpected(INVALID_HEADER);
  }

  if (!SeekTo(file, info.dataOffset)) {
    return etl::unexpected(INVALID_HEADER);
  }
  return info;
}
bool WavHeaderWriter::UpdateFileSize(I_File *file, uint32_t sampleCount,
                                     uint16_t channels,
                                     uint16_t bytesPerSample) {
  if (!file) {
    return false;
  }

  // Get the current position, which is the total file size
  uint32_t totalFileSize = file->Tell();
  if (totalFileSize < 44) {
    Trace::Error("WAVHEADER: file too small to patch header (%u bytes)",
                 totalFileSize);
    return false;
  }

  // Calculate the two size fields required by the WAV header
  uint32_t chunk_size = totalFileSize - 8;
  uint32_t subchunk2_size = sampleCount * channels * bytesPerSample;

  // Update ChunkSize (Total file size - 8)
  file->Seek(4, SEEK_SET);
  int written = file->Write(&chunk_size, 1, 4);
  if (written != 4) {
    Trace::Error("WAVHEADER: failed to write RIFF chunk size (wrote=%d err=%d)",
                 written, file->Error());
    return false;
  }

  Trace::Log("WAVHEADER", "Updating header: FileSize=%u, DataSize=%u",
             chunk_size, subchunk2_size);

  // Update Subchunk2Size (the size of the raw data)
  file->Seek(40, SEEK_SET);
  written = file->Write(&subchunk2_size, 1, 4);
  if (written != 4) {
    Trace::Error("WAVHEADER: failed to write data chunk size (wrote=%d err=%d)",
                 written, file->Error());
    return false;
  }

  // Return the file pointer to its original position at the end of the file
  file->Seek(totalFileSize, SEEK_SET);

  // Force a sync to write all cached data to the disk before closing.
  return file->Sync();
}
