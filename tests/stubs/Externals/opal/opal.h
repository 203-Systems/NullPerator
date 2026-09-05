/* Deterministic host decoder boundary for OpalInstrument register coverage. */
#pragma once

#include "Foundation/Types/Fixed.h"

#include <cstdint>

class Opal {
public:
  Opal(int sampleRate) { (void)sampleRate; }

  void Port(std::uint16_t registerNumber, std::uint8_t value) {
    lastPortRegister_ = registerNumber;
    lastPortValue_ = value;
  }

  void SampleBuffer(fixed *buffer, int size) {
    (void)buffer;
    (void)size;
  }

  static std::uint16_t LastPortRegister() { return lastPortRegister_; }
  static std::uint8_t LastPortValue() { return lastPortValue_; }

private:
  inline static std::uint16_t lastPortRegister_ = 0U;
  inline static std::uint8_t lastPortValue_ = 0U;
};
