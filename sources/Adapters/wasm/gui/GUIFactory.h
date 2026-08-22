/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "UIFramework/Interfaces/I_GUIWindowFactory.h"

class GUIFactory final : public I_GUIWindowFactory {
public:
  I_GUIWindowImp &CreateWindowImp(GUICreateWindowParams &params) override;
  EventManager *GetEventManager() override;
};
