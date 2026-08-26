/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Adapters/wasm/gui/WasmUiPresenter.h"

#include <SDL.h>
#include <emscripten/html5_webgl.h>

#include <array>
#include <cstdint>
#include <mutex>

class WasmGUIWindowImp final : public ui2::IUiPresenter {
public:
  static constexpr int CanvasWidth = 240;
  static constexpr int CanvasHeight = 240;
  static constexpr int SourceWidth = 240;
  static constexpr int SourceHeight = 240;

  WasmGUIWindowImp();
  ~WasmGUIWindowImp() override;
  bool HasPresentedFrame() const;
  static const std::uint8_t *CaptureFrameRgba();
  static const std::uint32_t *FrameSnapshotSequence();
  ui2::PresentResult
  Present(const ui2::UiIndexedSurface &surface,
          const ui2::UiPalette &palette,
          std::span<const ui2::DirtyStrip> strips) override;

private:
  using RgbaFrame =
      std::array<std::uint8_t, CanvasWidth * CanvasHeight * 4>;

  bool InitializePresenter();
  void DestroyPresenter();
  bool PresentFrame(const RgbaFrame &frame);
  static bool CommitUi2Frame(void *context);

  SDL_Window *window_ = nullptr;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_ = 0;
  unsigned int shaderProgram_ = 0;
  unsigned int texture_ = 0;
  unsigned int vertexBuffer_ = 0;
  int positionLocation_ = -1;
  int textureLocation_ = -1;
  RgbaFrame ui2Frame_{};
  WasmUiPresenter ui2Presenter_;
  std::recursive_mutex mutex_;
  bool hasPresentedFrame_ = false;

  static WasmGUIWindowImp *instance_;
};

extern "C" const std::uint8_t *PicoTracker_Wasm_CaptureFrameRgba();
extern "C" const std::uint32_t *PicoTracker_Wasm_GetFrameSnapshotSequence();
