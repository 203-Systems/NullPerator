/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

// Product identity is independent from the compatibility version written into
// project and instrument files.
namespace nullperator_product {

inline constexpr char Version[] = "0.1.1";
// UI branding must not be used as the serialized project version.
inline constexpr char DisplayVersion[] = "NullPerator 0.1.1";

} // namespace nullperator_product
