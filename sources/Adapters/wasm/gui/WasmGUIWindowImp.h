/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UIFramework/Interfaces/I_GUIWindowImp.h"
#include "Adapters/wasm/gui/WasmUiPresenter.h"

#include <SDL.h>
#include <emscripten/html5_webgl.h>

#include <array>
#include <cstdint>
#include <mutex>

class WasmGUIWindowImp final : public I_GUIWindowImp,
                               public ui2::IUiPresenter {
public:
  static constexpr int CanvasWidth = 240;
  static constexpr int CanvasHeight = 240;
  static constexpr int SourceWidth = 240;
  static constexpr int SourceHeight = 240;

  explicit WasmGUIWindowImp(GUICreateWindowParams &params);
  ~WasmGUIWindowImp() override;

  void SetColor(GUIColor &color) override;
  void DrawRect(GUIRect &rect) override;
  void DrawChar(char character, const GUIPoint &position,
                const GUITextProperties &properties) override;
  void DrawString(const char *string, const GUIPoint &position,
                  const GUITextProperties &properties,
                  bool overlay = false) override;
  void ClearTextRect(GUIRect &rect) override;
  GUIRect GetRect() override;
  void Invalidate() override;
  void Flush() override;
  void Lock() override;
  void Unlock() override;
  void Clear(GUIColor &color, bool overlay = false) override;
  void PushEvent(GUIEvent &event) override;

  void ProcessExpose();
  void ProcessQuit();
  void SetUi2DisplayOwnership(bool ownsDisplay);
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

  static SDL_Rect TransformRect(const GUIRect &rect);
  static std::uint32_t ClampColor(const GUIColor &color);
  void FillRect(const SDL_Rect &rect, std::uint32_t color);
  void MarkDirty(const SDL_Rect &rect);
  void DrawGlyph(std::uint8_t character, int cellX, int cellY,
                 bool inverted);
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
  RgbaFrame frame_{};
  RgbaFrame ui2Frame_{};
  WasmUiPresenter ui2Presenter_;
  std::recursive_mutex mutex_;
  std::uint32_t currentColor_ = 0xADADADFFu;
  std::uint32_t backgroundColor_ = 0x0F0F0FFFu;
  SDL_Rect dirtyRect_{0, 0, 0, 0};
  bool dirty_ = false;
  bool hasPresentedFrame_ = false;
  bool ui2OwnsDisplay_ = false;

  static WasmGUIWindowImp *instance_;
};

extern "C" const std::uint8_t *PicoTracker_Wasm_CaptureFrameRgba();
extern "C" const std::uint32_t *PicoTracker_Wasm_GetFrameSnapshotSequence();
