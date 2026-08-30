/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Header-light diagnostics boundary for the strict -Werror exported bridge.
 */

#pragma once

#include <cstdint>

// Stable browser diagnostics ABI. These values intentionally preserve the
// historical numeric view IDs without pulling the legacy View hierarchy into
// the native UI2 product.
enum WasmDiagnosticView : std::uint32_t {
  VT_SONG = 0,
  VT_CHAIN,
  VT_PHRASE,
  VT_PROJECT,
  VT_DEVICE,
  VT_INSTRUMENT,
  VT_TABLE,
  VT_TABLE2,
  VT_GROOVE,
  VT_MIXER,
  VT_IMPORT,
  VT_INSTRUMENT_IMPORT,
  VT_SELECTPROJECT,
  VT_THEME,
  VT_SELECTTHEME,
  VT_THEME_IMPORT,
  VT_SAMPLE_EDITOR,
  VT_SAMPLE_SLICES,
  VT_RECORD,
  VT_FONT,
};

static_assert(VT_RECORD == 18U);
static_assert(VT_FONT == 19U);

void WasmViewDiagnostics_Request(std::uint32_t viewType) noexcept;
std::uint32_t WasmViewDiagnostics_Current() noexcept;
std::uint32_t WasmViewDiagnostics_ViewGeneration() noexcept;
std::uint32_t WasmViewDiagnostics_InputGeneration() noexcept;
void WasmViewDiagnostics_RequestModal(std::uint32_t modalType) noexcept;
std::uint32_t WasmViewDiagnostics_CurrentModal() noexcept;
std::uint32_t WasmViewDiagnostics_ModalGeneration() noexcept;
