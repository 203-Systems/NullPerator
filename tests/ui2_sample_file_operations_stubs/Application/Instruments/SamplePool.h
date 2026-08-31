#pragma once

#include "Application/Persistency/PersistenceConstants.h"
#include "Externals/etl/include/etl/string.h"

#include <array>
#include <cstdint>
#include <cstring>

class SamplePool {
public:
  int GetNameListSize() { return count_; }

  std::uint32_t FindSampleIndexByName(
      const etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &name) {
    for (int index = 0; index < count_; ++index) {
      if (std::strcmp(names_[index].data(), name.c_str()) == 0)
        return static_cast<std::uint32_t>(index);
    }
    return UINT32_MAX;
  }

  bool unloadSample(std::uint32_t index) {
    if (unloadFails_ || index >= static_cast<std::uint32_t>(count_))
      return false;
    for (int current = static_cast<int>(index); current + 1 < count_;
         ++current) {
      names_[current] = names_[current + 1];
    }
    --count_;
    return true;
  }

  void SetSample(const char *name) {
    count_ = 1;
    names_[0].fill('\0');
    if (name != nullptr)
      std::strncpy(names_[0].data(), name, names_[0].size() - 1U);
  }

  void SetUnloadFails(bool fails) { unloadFails_ = fails; }

private:
  std::array<std::array<char, MAX_INSTRUMENT_FILENAME_LENGTH + 1U>, 2U>
      names_{};
  int count_ = 0;
  bool unloadFails_ = false;
};
