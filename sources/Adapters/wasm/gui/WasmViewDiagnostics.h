/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Header-light diagnostics boundary for the strict -Werror exported bridge.
 */

#pragma once

#include <cstdint>

void WasmViewDiagnostics_Request(std::uint32_t viewType) noexcept;
std::uint32_t WasmViewDiagnostics_Current() noexcept;
std::uint32_t WasmViewDiagnostics_ViewGeneration() noexcept;
std::uint32_t WasmViewDiagnostics_InputGeneration() noexcept;
void WasmViewDiagnostics_RequestModal(std::uint32_t modalType) noexcept;
std::uint32_t WasmViewDiagnostics_CurrentModal() noexcept;
std::uint32_t WasmViewDiagnostics_ModalGeneration() noexcept;
