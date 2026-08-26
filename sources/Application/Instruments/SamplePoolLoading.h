/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SAMPLE_POOL_LOADING_H_
#define _SAMPLE_POOL_LOADING_H_

#include "System/FileSystem/FileSystem.h"

#include <cstddef>

namespace SamplePoolLoading {

// Enters a project's sample directory and obtains a complete checked listing.
// Empty is a valid result; navigation, enumeration, or fixed-capacity
// truncation is not. Keeping this boundary separate lets Session distinguish
// an empty pool from an I/O failure without allocating a manifest.
bool EnterAndList(FileSystem &fileSystem, const char *projectName,
                  etl::ivector<int> &fileIndexes);

// Directory adapters may expose navigation rows (for example "..") alongside
// filtered WAV entries. Count only files whose names the sample pool can
// actually retain; otherwise a valid full-capacity project can be rejected one
// row early merely because it is not located at the filesystem root.
bool FitsLoadableSampleCapacity(FileSystem &fileSystem,
                                const etl::ivector<int> &fileIndexes,
                                std::size_t alreadyLoaded,
                                std::size_t capacity);

} // namespace SamplePoolLoading

#endif
