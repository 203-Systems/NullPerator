/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UIFramework/Interfaces/I_GUIWindowImp.h"

#include <SDL.h>
#include <emscripten/html5_webgl.h>

#include <array>
#include <cstdint>
#include <mutex>

class WasmGUIWindowImp final : public I_GUIWindowImp {
public:
  static constexpr int CanvasWidth = 240;
  static constexpr int CanvasHeight = 240;
  static constexpr int SourceWidth = 320;
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
  bool HasPresentedFrame() const;
  static const std::uint8_t *CaptureFrameRgba();

private:
  static SDL_Rect TransformRect(const GUIRect &rect);
  static std::uint32_t ClampColor(const GUIColor &color);
  void FillRect(const SDL_Rect &rect, std::uint32_t color);
  void MarkDirty(const SDL_Rect &rect);
  void DrawGlyph(std::uint8_t character, int cellX, int cellY,
                 bool inverted);
  bool InitializePresenter();
  void DestroyPresenter();
  bool PresentFrame();

  SDL_Window *window_ = nullptr;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_ = 0;
  unsigned int shaderProgram_ = 0;
  unsigned int texture_ = 0;
  unsigned int vertexBuffer_ = 0;
  int positionLocation_ = -1;
  int textureLocation_ = -1;
  std::array<std::uint8_t, CanvasWidth * CanvasHeight * 4> frame_{};
  std::array<std::uint8_t, CanvasWidth * CanvasHeight * 4> capture_{};
  std::recursive_mutex mutex_;
  std::uint32_t currentColor_ = 0xADADADFFu;
  std::uint32_t backgroundColor_ = 0x0F0F0FFFu;
  SDL_Rect dirtyRect_{0, 0, 0, 0};
  bool dirty_ = false;
  bool hasPresentedFrame_ = false;

  static WasmGUIWindowImp *instance_;
};

extern "C" const std::uint8_t *PicoTracker_Wasm_CaptureFrameRgba();
