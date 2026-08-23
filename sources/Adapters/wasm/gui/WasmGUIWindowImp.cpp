/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Adapters/wasm/gui/WasmGUIWindowImp.h"
#include "Adapters/wasm/gui/WasmFrameSnapshot.h"
#include "Adapters/wasm/tracing/InputFrameLatencyTracker.h"

#include <emscripten/emscripten.h>

#include "Adapters/wasm/gui/font.h"
#include "Application/Model/Config.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"

#include <GLES2/gl2.h>
#include <algorithm>
#include <cstring>

namespace {
constexpr int CellSourceWidth = 8;
constexpr int CellSourceHeight = 10;
constexpr int FrameworkCellStep = 8;
constexpr int TextColumns = WasmGUIWindowImp::SourceWidth / CellSourceWidth;
constexpr std::size_t FrameBytes =
    WasmGUIWindowImp::CanvasWidth * WasmGUIWindowImp::CanvasHeight * 4U;

constexpr bool IsGlyphPixelSet(std::uint16_t row, int sourceX) {
  return (row & (1u << sourceX)) != 0;
}

// The vertical stroke in the font's 'F' glyph is encoded as 0x02. It belongs
// near the left edge (x=1), not the right edge (x=8).
static_assert(IsGlyphPixelSet(0x02, 1));
static_assert(!IsGlyphPixelSet(0x02, 8));

WasmFrameSnapshot<FrameBytes> frameSnapshot;

int ScaleXFloor(int x) {
  return (x * WasmGUIWindowImp::CanvasWidth) /
         WasmGUIWindowImp::SourceWidth;
}

int ScaleXCeil(int x) {
  return (x * WasmGUIWindowImp::CanvasWidth +
          WasmGUIWindowImp::SourceWidth - 1) /
         WasmGUIWindowImp::SourceWidth;
}

void StorePixel(std::uint8_t *destination, std::uint32_t rgba) {
  destination[0] = static_cast<std::uint8_t>((rgba >> 24) & 0xFF);
  destination[1] = static_cast<std::uint8_t>((rgba >> 16) & 0xFF);
  destination[2] = static_cast<std::uint8_t>((rgba >> 8) & 0xFF);
  destination[3] = static_cast<std::uint8_t>(rgba & 0xFF);
}

constexpr char VertexShaderSource[] = R"(
attribute vec2 a_position;
attribute vec2 a_texcoord;
varying vec2 v_texcoord;
void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
  v_texcoord = a_texcoord;
}
)";

constexpr char FragmentShaderSource[] = R"(
precision mediump float;
uniform sampler2D u_texture;
varying vec2 v_texcoord;
void main() {
  gl_FragColor = texture2D(u_texture, v_texcoord);
}
)";

unsigned int CompileShader(unsigned int type, const char *source) {
  const unsigned int shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  int compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == 0) {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}
} // namespace

WasmGUIWindowImp *WasmGUIWindowImp::instance_ = nullptr;

WasmGUIWindowImp::WasmGUIWindowImp(GUICreateWindowParams &params) {
  (void)params;
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#ifdef SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR
  SDL_SetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR, "#picotracker-canvas");
#endif
  window_ = SDL_CreateWindow("PicoTracker", SDL_WINDOWPOS_UNDEFINED,
                             SDL_WINDOWPOS_UNDEFINED, CanvasWidth,
                             CanvasHeight, SDL_WINDOW_SHOWN);
  if (window_ == nullptr) {
    return;
  }
  if (!InitializePresenter()) {
    return;
  }
  instance_ = this;
  std::fill(frame_.begin(), frame_.end(), 0);
  for (std::size_t i = 3; i < frame_.size(); i += 4) {
    frame_[i] = 0xFF;
  }
  dirtyRect_ = {0, 0, CanvasWidth, CanvasHeight};
  dirty_ = true;
}

WasmGUIWindowImp::~WasmGUIWindowImp() {
  if (instance_ == this) {
    instance_ = nullptr;
  }
  DestroyPresenter();
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
  }
}

std::uint32_t WasmGUIWindowImp::ClampColor(const GUIColor &color) {
  const auto r = static_cast<std::uint32_t>(std::min<unsigned short>(color._r, 255));
  const auto g = static_cast<std::uint32_t>(std::min<unsigned short>(color._g, 255));
  const auto b = static_cast<std::uint32_t>(std::min<unsigned short>(color._b, 255));
  return (r << 24) | (g << 16) | (b << 8) | 0xFFu;
}

void WasmGUIWindowImp::SetColor(GUIColor &color) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  currentColor_ = ClampColor(color);
}

SDL_Rect WasmGUIWindowImp::TransformRect(const GUIRect &rect) {
  const int left = std::clamp(ScaleXFloor(rect.Left()), 0, CanvasWidth);
  const int right = std::clamp(ScaleXCeil(rect.Right()), 0, CanvasWidth);
  const int top = std::clamp(rect.Top(), 0, CanvasHeight);
  const int bottom = std::clamp(rect.Bottom(), 0, CanvasHeight);
  return SDL_Rect{left, top, std::max(0, right - left),
                  std::max(0, bottom - top)};
}

void WasmGUIWindowImp::FillRect(const SDL_Rect &rect, std::uint32_t color) {
  const int left = std::clamp(rect.x, 0, CanvasWidth);
  const int top = std::clamp(rect.y, 0, CanvasHeight);
  const int right = std::clamp(rect.x + rect.w, 0, CanvasWidth);
  const int bottom = std::clamp(rect.y + rect.h, 0, CanvasHeight);
  if (left >= right || top >= bottom) {
    return;
  }
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      StorePixel(frame_.data() + (y * CanvasWidth + x) * 4, color);
    }
  }
  MarkDirty(SDL_Rect{left, top, right - left, bottom - top});
}

void WasmGUIWindowImp::MarkDirty(const SDL_Rect &rect) {
  if (rect.w <= 0 || rect.h <= 0) {
    return;
  }
  if (!dirty_) {
    dirtyRect_ = rect;
    dirty_ = true;
    return;
  }
  const int left = std::min(dirtyRect_.x, rect.x);
  const int top = std::min(dirtyRect_.y, rect.y);
  const int right = std::max(dirtyRect_.x + dirtyRect_.w, rect.x + rect.w);
  const int bottom = std::max(dirtyRect_.y + dirtyRect_.h, rect.y + rect.h);
  dirtyRect_ = {left, top, right - left, bottom - top};
}

void WasmGUIWindowImp::DrawRect(GUIRect &rect) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  FillRect(TransformRect(rect), currentColor_);
}

void WasmGUIWindowImp::DrawGlyph(std::uint8_t character, int cellX,
                                 int cellY, bool inverted) {
  if (cellX < 0 || cellX >= TextColumns || cellY < 0 || cellY >= 24) {
    return;
  }
  const int left = ScaleXFloor(cellX * CellSourceWidth);
  const int right = ScaleXFloor((cellX + 1) * CellSourceWidth);
  const int top = cellY * CellSourceHeight;
  const int bottom = top + CellSourceHeight;
  const std::uint32_t foreground = inverted ? backgroundColor_ : currentColor_;
  const std::uint32_t background = inverted ? currentColor_ : backgroundColor_;

  const std::uint16_t *regularRows = nullptr;
  const std::uint8_t *specialRows = nullptr;
  if (character >= 32 && character < 128) {
    int fontIndex = 0;
    Config *config = Config::GetInstance();
    if (config != nullptr) {
      Variable *fontVariable = config->FindVariable(FourCC::VarUIFont);
      if (fontVariable != nullptr) {
        fontIndex = fontVariable->GetInt();
      }
    }
    const std::size_t glyphIndex = character - 32;
    regularRows = fontIndex == 0 ? FONT_STEALTH57_BITMAP[glyphIndex]
                                 : FONT_YOU_SQUARED_BITMAP[glyphIndex];
  } else if (character >= 128) {
    specialRows = FONT_SPECIAL_CHARACTERS_BITMAP[character - 128];
  }

  for (int y = top; y < bottom; ++y) {
    const int sourceY = y - top;
    const std::uint16_t row = regularRows != nullptr
                                  ? regularRows[sourceY]
                              : specialRows != nullptr
                                  ? specialRows[sourceY]
                                  : 0;
    for (int x = left; x < right; ++x) {
      const int sourceX = ((x - left) * CellSourceWidth) /
                          std::max(1, right - left);
      // Match the Node display writer: pixel x uses mask (1 << x), so bit 0
      // is the leftmost pixel in an ordinary browser framebuffer.
      const bool set = IsGlyphPixelSet(row, sourceX);
      StorePixel(frame_.data() + (y * CanvasWidth + x) * 4,
                 set ? foreground : background);
    }
  }
  MarkDirty(SDL_Rect{left, top, right - left, bottom - top});
}

void WasmGUIWindowImp::DrawChar(char character, const GUIPoint &position,
                                const GUITextProperties &properties) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  DrawGlyph(static_cast<std::uint8_t>(character),
            static_cast<int>(position._x / FrameworkCellStep),
            static_cast<int>(position._y / FrameworkCellStep),
            properties.invert_);
}

void WasmGUIWindowImp::DrawString(const char *string,
                                  const GUIPoint &position,
                                  const GUITextProperties &properties,
                                  bool overlay) {
  (void)overlay;
  if (string == nullptr) {
    return;
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  int cellX = static_cast<int>(position._x / FrameworkCellStep);
  const int cellY = static_cast<int>(position._y / FrameworkCellStep);
  while (*string != '\0' && cellX < TextColumns) {
    DrawGlyph(static_cast<std::uint8_t>(*string), cellX, cellY,
              properties.invert_);
    ++string;
    ++cellX;
  }
}

void WasmGUIWindowImp::ClearTextRect(GUIRect &rect) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  FillRect(TransformRect(rect), backgroundColor_);
}

GUIRect WasmGUIWindowImp::GetRect() {
  return GUIRect(0, 0, SourceWidth, SourceHeight);
}

void WasmGUIWindowImp::Invalidate() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  dirtyRect_ = {0, 0, CanvasWidth, CanvasHeight};
  dirty_ = true;
  if (_window != nullptr) {
    _window->Update(true);
  }
}

void WasmGUIWindowImp::Flush() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!dirty_ || context_ <= 0) {
    InputFrameLatencyTracker::ObserveNoPresentation();
    return;
  }
  if (PresentFrame()) {
    // PresentFrame returns true only after explicit WebGL swap control commits
    // successfully. This is the presentation boundary, not DispatchEvent.
    InputFrameLatencyTracker::PresentedFrame();
    frameSnapshot.Publish(frame_);
    dirty_ = false;
    dirtyRect_ = {0, 0, 0, 0};
    hasPresentedFrame_ = true;
  } else {
    InputFrameLatencyTracker::ObserveNoPresentation();
  }
}

void WasmGUIWindowImp::Lock() { mutex_.lock(); }

void WasmGUIWindowImp::Unlock() { mutex_.unlock(); }

void WasmGUIWindowImp::Clear(GUIColor &color, bool overlay) {
  (void)overlay;
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  backgroundColor_ = ClampColor(color);
  FillRect(SDL_Rect{0, 0, CanvasWidth, CanvasHeight}, backgroundColor_);
}

void WasmGUIWindowImp::PushEvent(GUIEvent &event) {
  SDL_Event sdlEvent{};
  sdlEvent.type = SDL_USEREVENT;
  sdlEvent.user.code = 1;
  sdlEvent.user.data1 = new GUIEvent(event);
  if (SDL_PushEvent(&sdlEvent) < 0) {
    delete static_cast<GUIEvent *>(sdlEvent.user.data1);
  }
}

void WasmGUIWindowImp::ProcessExpose() {
  if (_window != nullptr) {
    _window->Update(true);
  }
}

void WasmGUIWindowImp::ProcessQuit() {
  if (_window == nullptr) {
    return;
  }
  GUIPoint position;
  GUIEvent event(position, ET_SYSQUIT);
  _window->DispatchEvent(event);
}

bool WasmGUIWindowImp::HasPresentedFrame() const { return hasPresentedFrame_; }

bool WasmGUIWindowImp::InitializePresenter() {
  EmscriptenWebGLContextAttributes attributes;
  emscripten_webgl_init_context_attributes(&attributes);
  attributes.alpha = EM_FALSE;
  attributes.antialias = EM_FALSE;
  attributes.depth = EM_FALSE;
  attributes.stencil = EM_FALSE;
  attributes.explicitSwapControl = EM_TRUE;
  attributes.proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_DISALLOW;
  attributes.majorVersion = 1;
  context_ = emscripten_webgl_create_context("#picotracker-canvas", &attributes);
  if (context_ <= 0 ||
      emscripten_webgl_make_context_current(context_) != EMSCRIPTEN_RESULT_SUCCESS) {
    context_ = 0;
    return false;
  }

  const unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, VertexShaderSource);
  const unsigned int fragmentShader =
      CompileShader(GL_FRAGMENT_SHADER, FragmentShaderSource);
  if (vertexShader == 0 || fragmentShader == 0) {
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    DestroyPresenter();
    return false;
  }
  shaderProgram_ = glCreateProgram();
  glAttachShader(shaderProgram_, vertexShader);
  glAttachShader(shaderProgram_, fragmentShader);
  glLinkProgram(shaderProgram_);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  int linked = 0;
  glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &linked);
  if (linked == 0) {
    DestroyPresenter();
    return false;
  }

  positionLocation_ = glGetAttribLocation(shaderProgram_, "a_position");
  textureLocation_ = glGetAttribLocation(shaderProgram_, "a_texcoord");
  if (positionLocation_ < 0 || textureLocation_ < 0) {
    DestroyPresenter();
    return false;
  }

  constexpr float vertices[] = {
      -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
      -1.0f, 1.0f, 0.0f, 0.0f,  1.0f, 1.0f, 1.0f, 0.0f,
  };
  glGenBuffers(1, &vertexBuffer_);
  glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CanvasWidth, CanvasHeight, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  return glGetError() == GL_NO_ERROR;
}

void WasmGUIWindowImp::DestroyPresenter() {
  if (context_ > 0) {
    emscripten_webgl_make_context_current(context_);
    if (texture_ != 0) {
      glDeleteTextures(1, &texture_);
    }
    if (vertexBuffer_ != 0) {
      glDeleteBuffers(1, &vertexBuffer_);
    }
    if (shaderProgram_ != 0) {
      glDeleteProgram(shaderProgram_);
    }
    emscripten_webgl_destroy_context(context_);
  }
  context_ = 0;
  shaderProgram_ = 0;
  texture_ = 0;
  vertexBuffer_ = 0;
}

bool WasmGUIWindowImp::PresentFrame() {
  if (emscripten_webgl_make_context_current(context_) != EMSCRIPTEN_RESULT_SUCCESS) {
    return false;
  }
  glViewport(0, 0, CanvasWidth, CanvasHeight);
  glUseProgram(shaderProgram_);
  glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
  glEnableVertexAttribArray(static_cast<unsigned int>(positionLocation_));
  glEnableVertexAttribArray(static_cast<unsigned int>(textureLocation_));
  glVertexAttribPointer(static_cast<unsigned int>(positionLocation_), 2, GL_FLOAT,
                        GL_FALSE, 4 * sizeof(float), nullptr);
  glVertexAttribPointer(static_cast<unsigned int>(textureLocation_), 2, GL_FLOAT,
                        GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<const void *>(2 * sizeof(float)));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CanvasWidth, CanvasHeight, GL_RGBA,
                  GL_UNSIGNED_BYTE, frame_.data());
  glUniform1i(glGetUniformLocation(shaderProgram_, "u_texture"), 0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  return glGetError() == GL_NO_ERROR &&
         emscripten_webgl_commit_frame() == EMSCRIPTEN_RESULT_SUCCESS;
}

const std::uint8_t *WasmGUIWindowImp::CaptureFrameRgba() {
  return frameSnapshot.Data();
}

const std::uint32_t *WasmGUIWindowImp::FrameSnapshotSequence() {
  return frameSnapshot.SequenceAddress();
}

const std::uint8_t *Wasm_FrameSnapshotAddress() noexcept {
  return WasmGUIWindowImp::CaptureFrameRgba();
}

const std::uint32_t *Wasm_FrameSequenceAddress() noexcept {
  return WasmGUIWindowImp::FrameSnapshotSequence();
}

extern "C" EMSCRIPTEN_KEEPALIVE const std::uint8_t *
PicoTracker_Wasm_CaptureFrameRgba() {
  return WasmGUIWindowImp::CaptureFrameRgba();
}

extern "C" EMSCRIPTEN_KEEPALIVE const std::uint32_t *
PicoTracker_Wasm_GetFrameSnapshotSequence() {
  return WasmGUIWindowImp::FrameSnapshotSequence();
}
