/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SAMPLE_BINDING_STATE_H_
#define _SAMPLE_BINDING_STATE_H_

#include "Application/Persistency/PersistenceConstants.h"
#include "Externals/etl/include/etl/string.h"

// A project may legitimately reference a WAV that is temporarily absent from
// its samples directory. Keep that user data independently of the pool index
// so an autosave cannot turn the reference into an empty selection.
class SampleBindingState {
public:
  void Capture(const char *requestedName, int resolvedIndex) {
    if (resolvedIndex < 0 && requestedName != nullptr &&
        requestedName[0] != '\0') {
      unresolvedName_ = requestedName;
    } else {
      unresolvedName_.clear();
    }
  }

  void Clear() { unresolvedName_.clear(); }
  [[nodiscard]] bool HasUnresolvedName() const {
    return !unresolvedName_.empty();
  }
  [[nodiscard]] const char *UnresolvedName() const {
    return unresolvedName_.c_str();
  }

private:
  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> unresolvedName_;
};

#endif
