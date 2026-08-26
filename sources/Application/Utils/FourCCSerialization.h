/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#ifndef _FOURCC_SERIALIZATION_H_
#define _FOURCC_SERIALIZATION_H_

#include "Foundation/Types/Types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace FourCCSerialization {

inline bool IsInstrumentCommand(std::uint8_t value) {
  switch (static_cast<FourCC::enum_type>(value)) {
  case FourCC::InstrumentCommandArpeggiator:
  case FourCC::InstrumentCommandCrush:
  case FourCC::InstrumentCommandDelay:
  case FourCC::InstrumentCommandFilterCut:
  case FourCC::InstrumentCommandLowPassFilter:
  case FourCC::InstrumentCommandFilterResonance:
  case FourCC::InstrumentCommandGateOff:
  case FourCC::InstrumentCommandGroove:
  case FourCC::InstrumentCommandHop:
  case FourCC::InstrumentCommandRetrigger:
  case FourCC::InstrumentCommandInstrumentRetrigger:
  case FourCC::InstrumentCommandKill:
  case FourCC::InstrumentCommandLegato:
  case FourCC::InstrumentCommandLoopOfset:
  case FourCC::InstrumentCommandMidiCC:
  case FourCC::InstrumentCommandMidiPC:
  case FourCC::InstrumentCommandPan:
  case FourCC::InstrumentCommandPitchFineTune:
  case FourCC::InstrumentCommandPlayOfset:
  case FourCC::InstrumentCommandPitchSlide:
  case FourCC::InstrumentCommandStop:
  case FourCC::InstrumentCommandTable:
  case FourCC::InstrumentCommandTempo:
  case FourCC::InstrumentCommandVelocity:
  case FourCC::InstrumentCommandVolume:
  case FourCC::InstrumentCommandNone:
  case FourCC::InstrumentCommandMidiChord:
    return true;
  default:
    return false;
  }
}

inline FourCC DecodeCommand(std::uint8_t value) {
  return IsInstrumentCommand(value)
             ? FourCC(static_cast<FourCC::enum_type>(value))
             : FourCC(FourCC::InstrumentCommandNone);
}

// Streaming decoder used by XML persistence. It stages at most the raw
// canonical prefix in the caller-owned destination and then compacts it in
// place when the input grows past the 1-byte or 2-byte ABI boundary. This is
// deliberately allocation-free: the largest Song command buffer is 2048
// commands (8192 legacy LE32 bytes), but decoder stack state stays constant.
class CommandStreamDecoder {
public:
  CommandStreamDecoder(FourCC *output, std::size_t count)
      : output_(output), count_(output == nullptr ? 0U : count),
        le16Limit_(SaturatingMultiply(count_, 2U)),
        le32Limit_(SaturatingMultiply(count_, 4U)) {
    if (output_ != nullptr)
      std::fill_n(output_, count_, FourCC::InstrumentCommandNone);
  }

  void Push(std::uint8_t byte) {
    if (finalized_ || totalBytes_ >= le32Limit_)
      return;

    if (mode_ == Mode::Byte && totalBytes_ == count_)
      ConvertRawToLe16();
    if (mode_ == Mode::Le16 && totalBytes_ == le16Limit_)
      ConvertLe16ToLe32();

    switch (mode_) {
    case Mode::Byte:
      output_[totalBytes_] =
          FourCC(static_cast<FourCC::enum_type>(byte));
      break;
    case Mode::Le16:
      ConsumeLe16Byte(byte);
      break;
    case Mode::Le32:
      ConsumeLe32Byte(byte);
      break;
    }
    ++totalBytes_;
  }

  void Push(std::span<const std::uint8_t> bytes) {
    for (const std::uint8_t byte : bytes)
      Push(byte);
  }

  // RLE lengths in project XML are untrusted. Process only the bytes that can
  // affect the destination instead of looping over a malicious LENGTH value.
  void PushRepeated(std::uint8_t byte, std::size_t count) {
    const std::size_t remaining =
        totalBytes_ < le32Limit_ ? le32Limit_ - totalBytes_ : 0U;
    const std::size_t process = std::min(count, remaining);
    for (std::size_t index = 0; index < process; ++index)
      Push(byte);
  }

  void Finalize() {
    if (finalized_ || output_ == nullptr) {
      finalized_ = true;
      return;
    }

    // Exact canonical (count) and LE16 (2 * count) lengths take precedence.
    // A LE32 file truncated to exactly one of those complete ABI lengths is
    // byte-for-byte ambiguous, so no decoder can identify it reliably.
    if (mode_ == Mode::Byte) {
      if (totalBytes_ < count_ && LooksLikeTruncatedLe32Raw()) {
        ConvertRawToLe32();
      } else if (totalBytes_ < count_ && LooksLikeTruncatedLe16Raw()) {
        ConvertRawToLe16();
      } else {
        ValidateCanonical();
      }
    } else if (mode_ == Mode::Le16 && totalBytes_ != le16Limit_ &&
               LooksLikeTruncatedLe32FromLe16()) {
      ConvertLe16ToLe32();
    }
    finalized_ = true;
  }

  [[nodiscard]] std::size_t InputBytes() const { return totalBytes_; }

private:
  enum class Mode : std::uint8_t { Byte, Le16, Le32 };

  static std::size_t SaturatingMultiply(std::size_t value,
                                        std::size_t multiplier) {
    return value > std::numeric_limits<std::size_t>::max() / multiplier
               ? std::numeric_limits<std::size_t>::max()
               : value * multiplier;
  }

  [[nodiscard]] std::uint8_t RawByte(std::size_t index) const {
    return static_cast<std::uint8_t>(output_[index].get_value());
  }

  static FourCC DecodeLe16(std::uint8_t low, std::uint8_t high) {
    return high == 0U ? DecodeCommand(low)
                      : FourCC(FourCC::InstrumentCommandNone);
  }

  static FourCC DecodeLe32(const std::uint8_t *word) {
    return word[1] == 0U && word[2] == 0U && word[3] == 0U
               ? DecodeCommand(word[0])
               : FourCC(FourCC::InstrumentCommandNone);
  }

  void ValidateCanonical() {
    const std::size_t available = std::min(totalBytes_, count_);
    for (std::size_t index = 0; index < available; ++index)
      output_[index] = DecodeCommand(RawByte(index));
    std::fill(output_ + available, output_ + count_,
              FourCC::InstrumentCommandNone);
  }

  void ConvertRawToLe16() {
    const std::size_t rawCount = std::min(totalBytes_, count_);
    const bool hasPending = (rawCount & 1U) != 0U;
    const std::uint8_t pending = hasPending ? RawByte(rawCount - 1U) : 0U;
    const std::size_t words = rawCount / 2U;
    for (std::size_t index = 0; index < words; ++index) {
      const std::size_t offset = index * 2U;
      const std::uint8_t low = RawByte(offset);
      const std::uint8_t high = RawByte(offset + 1U);
      output_[index] = DecodeLe16(low, high);
    }
    std::fill(output_ + words, output_ + count_,
              FourCC::InstrumentCommandNone);
    decodedCount_ = words;
    wordCount_ = hasPending ? 1U : 0U;
    if (hasPending)
      word_[0] = pending;
    mode_ = Mode::Le16;
  }

  void ConvertRawToLe32() {
    const std::size_t rawCount = std::min(totalBytes_, count_);
    const std::size_t words = rawCount / 4U;
    for (std::size_t index = 0; index < words; ++index) {
      const std::size_t offset = index * 4U;
      const std::uint8_t bytes[4] = {
          RawByte(offset), RawByte(offset + 1U), RawByte(offset + 2U),
          RawByte(offset + 3U)};
      output_[index] = DecodeLe32(bytes);
    }
    std::fill(output_ + words, output_ + count_,
              FourCC::InstrumentCommandNone);
    decodedCount_ = words;
    wordCount_ = 0U;
    mode_ = Mode::Le32;
  }

  void ConvertLe16ToLe32() {
    // A LE32 value decoded provisionally as LE16 appears as [command, ARP]:
    // its upper 16-bit word is exactly zero, and command 0 is ARP.
    const bool hasLowWord = (decodedCount_ & 1U) != 0U;
    const FourCC lowWord =
        hasLowWord ? output_[decodedCount_ - 1U]
                   : FourCC(FourCC::InstrumentCommandNone);
    const std::size_t words = decodedCount_ / 2U;
    for (std::size_t index = 0; index < words; ++index) {
      const FourCC low = output_[index * 2U];
      const FourCC high = output_[index * 2U + 1U];
      output_[index] =
          static_cast<std::uint8_t>(high.get_value()) == 0U
              ? low
              : FourCC(FourCC::InstrumentCommandNone);
    }
    std::fill(output_ + words, output_ + count_,
              FourCC::InstrumentCommandNone);
    decodedCount_ = words;
    wordCount_ = 0U;
    if (hasLowWord) {
      word_[0] = static_cast<std::uint8_t>(lowWord.get_value());
      word_[1] = 0U;
      wordCount_ = 2U;
    }
    mode_ = Mode::Le32;
  }

  void ConsumeLe16Byte(std::uint8_t byte) {
    word_[wordCount_++] = byte;
    if (wordCount_ != 2U)
      return;
    if (decodedCount_ < count_)
      output_[decodedCount_++] = DecodeLe16(word_[0], word_[1]);
    wordCount_ = 0U;
  }

  void ConsumeLe32Byte(std::uint8_t byte) {
    word_[wordCount_++] = byte;
    if (wordCount_ != 4U)
      return;
    if (decodedCount_ < count_)
      output_[decodedCount_++] = DecodeLe32(word_);
    wordCount_ = 0U;
  }

  [[nodiscard]] bool LooksLikeTruncatedLe16Raw() const {
    if (totalBytes_ < 4U || (totalBytes_ & 1U) != 0U)
      return false;
    for (std::size_t offset = 1U; offset < totalBytes_; offset += 2U) {
      if (RawByte(offset) != 0U)
        return false;
    }
    return true;
  }

  [[nodiscard]] bool LooksLikeTruncatedLe32Raw() const {
    if (totalBytes_ < 8U || (totalBytes_ & 3U) != 0U)
      return false;
    for (std::size_t offset = 0U; offset < totalBytes_; offset += 4U) {
      if (RawByte(offset + 1U) != 0U || RawByte(offset + 2U) != 0U ||
          RawByte(offset + 3U) != 0U)
        return false;
    }
    return true;
  }

  [[nodiscard]] bool LooksLikeTruncatedLe32FromLe16() const {
    if ((totalBytes_ & 3U) != 0U || decodedCount_ < 2U ||
        (decodedCount_ & 1U) != 0U || wordCount_ != 0U)
      return false;
    for (std::size_t index = 1U; index < decodedCount_; index += 2U) {
      if (static_cast<std::uint8_t>(output_[index].get_value()) != 0U)
        return false;
    }
    return true;
  }

  FourCC *output_ = nullptr;
  std::size_t count_ = 0U;
  std::size_t le16Limit_ = 0U;
  std::size_t le32Limit_ = 0U;
  std::size_t totalBytes_ = 0U;
  std::size_t decodedCount_ = 0U;
  Mode mode_ = Mode::Byte;
  std::uint8_t word_[4]{};
  std::uint8_t wordCount_ = 0U;
  bool finalized_ = false;
};

// Decode command buffers written by both the canonical byte-sized format and
// historical builds that persisted FourCC::enum_type in their platform ABI.
// ARM builds commonly stored each enum in one byte, while WebAssembly/desktop
// builds stored the same value as a four-byte little-endian enum. The old saver
// multiplied by sizeof(enum_type), so both layouts contain every command; this
// is an encoding difference, not lost project data. Only genuinely truncated
// or invalid input is left as InstrumentCommandNone (rendered as "---").
inline void DecodeCommands(std::span<const std::uint8_t> bytes, FourCC *output,
                           std::size_t count) {
  CommandStreamDecoder decoder(output, count);
  decoder.Push(bytes);
  decoder.Finalize();
}

} // namespace FourCCSerialization

#endif
