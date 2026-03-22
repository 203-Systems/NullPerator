/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "NullView.h"
#include <Application/AppWindow.h>
#include <nanoprintf.h>

NullView::NullView(GUIWindow &w, ViewData *viewData) : View(w, viewData) {}

NullView::~NullView() {}

void NullView::ProcessButtonMask(unsigned short mask, bool pressed){

};

void NullView::DrawView() {

  Clear();
};

void NullView::OnPlayerUpdate(PlayerEventType, unsigned int tick){

};

void NullView::OnFocus(){};
