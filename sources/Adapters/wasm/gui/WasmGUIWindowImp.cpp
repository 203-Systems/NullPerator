/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Adapters/wasm/gui/WasmGUIWindowImp.h"
#include "Adapters/wasm/gui/WasmFrameSnapshot.h"
#include "Adapters/wasm/tracing/InputFrameLatencyTracker.h"

#include <emscripten/emscripten.h>

#include <GLES2/gl2.h>
#include <algorithm>

namespace {
constexpr std::size_t FrameBytes =
    WasmGUIWindowImp::CanvasWidth * WasmGUIWindowImp::CanvasHeight * 4U;

WasmFrameSnapshot<FrameBytes> frameSnapshot;

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

WasmGUIWindowImp::WasmGUIWindowImp()
    : ui2Presenter_(ui2Frame_.data(), ui2Frame_.size(),
                    &WasmGUIWindowImp::CommitUi2Frame, this) {
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
  std::fill(ui2Frame_.begin(), ui2Frame_.end(), 0);
  for (std::size_t i = 3; i < ui2Frame_.size(); i += 4) {
    ui2Frame_[i] = 0xFF;
  }
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

bool WasmGUIWindowImp::HasPresentedFrame() const { return hasPresentedFrame_; }

ui2::PresentResult WasmGUIWindowImp::Present(
    const ui2::UiIndexedSurface &surface, const ui2::UiPalette &palette,
    std::span<const ui2::DirtyStrip> strips) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return ui2Presenter_.Present(surface, palette, strips);
}

bool WasmGUIWindowImp::CommitUi2Frame(void *context) {
  auto *window = static_cast<WasmGUIWindowImp *>(context);
  if (window == nullptr || window->context_ <= 0) return false;
  if (!window->PresentFrame(window->ui2Frame_)) {
    InputFrameLatencyTracker::ObserveNoPresentation();
    return false;
  }
  InputFrameLatencyTracker::PresentedFrame();
  frameSnapshot.Publish(window->ui2Frame_);
  window->hasPresentedFrame_ = true;
  return true;
}

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

bool WasmGUIWindowImp::PresentFrame(const RgbaFrame &frame) {
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
                  GL_UNSIGNED_BYTE, frame.data());
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
