/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core_link_closure.h"

#include "Application/AppWindow.h"
#include "Application/Application.h"
#include "Application/Player/Player.h"
#include "Application/Views/SongView.h"

namespace {
const auto applicationSymbol = &Application::Init;
const auto appWindowSymbol = &AppWindow::Create;
const auto playerSymbol = &Player::BindProject;
const auto viewSymbol = &SongView::Reset;
} // namespace

extern "C" const void *PicoTracker_Wasm_CoreApplicationAnchor() {
  return &applicationSymbol;
}

extern "C" const void *PicoTracker_Wasm_CoreAppWindowAnchor() {
  return &appWindowSymbol;
}

extern "C" const void *PicoTracker_Wasm_CorePlayerAnchor() {
  return &playerSymbol;
}

extern "C" const void *PicoTracker_Wasm_CoreViewAnchor() {
  return &viewSymbol;
}
