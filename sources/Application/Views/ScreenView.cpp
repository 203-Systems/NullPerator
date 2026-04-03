/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "ScreenView.h"
#include "Application/Player/Player.h"
#include <Application/AppWindow.h>
#include <nanoprintf.h>

ScreenView::ScreenView(GUIWindow &w, ViewData *viewData) : View(w, viewData) {}

ScreenView::~ScreenView() {}

bool ScreenView::ShouldDrawBattery() const { return true; }
bool ScreenView::ShouldDrawPlayTime() const { return true; }

/// Updates the animation by redrawing the battery gauge and power button UI on
/// every clock tick.
void ScreenView::AnimationUpdate() {
  Player *player = Player::GetInstance();
  GUITextProperties props;
  if (player && player->IsRunning() && ShouldDrawPlayTime()) {
    GUIPoint timePos(24, 1);
    SetColor(CD_NORMAL);
    drawPlayTime(player, timePos, props);
  } else if (ShouldDrawBattery()) {
    drawBattery(props);
  }
  drawPowerButtonUI(props);
};
