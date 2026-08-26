/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SAMPLE_POOL_LOADING_H_
#define _SAMPLE_POOL_LOADING_H_

#include "System/FileSystem/FileSystem.h"

namespace SamplePoolLoading {

// Enters a project's sample directory and obtains a complete checked listing.
// Empty is a valid result; navigation, enumeration, or fixed-capacity
// truncation is not. Keeping this boundary separate lets Session distinguish
// an empty pool from an I/O failure without allocating a manifest.
bool EnterAndList(FileSystem &fileSystem, const char *projectName,
                  etl::ivector<int> &fileIndexes);

} // namespace SamplePoolLoading

#endif
