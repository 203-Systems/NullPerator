/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Header-light bridge for selecting the native UI2 renderer.
 */

#pragma once

void WasmUi2_SetEnabled(bool enabled) noexcept;
bool WasmUi2_IsEnabled() noexcept;

