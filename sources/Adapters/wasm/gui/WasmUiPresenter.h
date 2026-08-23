/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Render/IUiPresenter.h"

#include <cstddef>
#include <cstdint>

class WasmUiPresenter final : public ui2::IUiPresenter {
public:
  using CommitFunction = bool (*)(void *context);

  WasmUiPresenter(std::uint8_t *rgbaFrame, std::size_t frameBytes,
                  CommitFunction commit, void *context)
      : rgbaFrame_(rgbaFrame), frameBytes_(frameBytes), commit_(commit),
        context_(context) {}

  ui2::PresentResult
  Present(const ui2::UiIndexedSurface &surface,
          const ui2::UiPalette &palette,
          std::span<const ui2::DirtyStrip> strips) override;

private:
  static constexpr std::size_t kRequiredBytes =
      static_cast<std::size_t>(ui2::kScreenWidth) * ui2::kScreenHeight * 4U;

  std::uint8_t *rgbaFrame_ = nullptr;
  std::size_t frameBytes_ = 0;
  CommitFunction commit_ = nullptr;
  void *context_ = nullptr;
};
