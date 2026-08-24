/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Rgb565DisplayTransport.h"

#include "Adapters/node/hal/nullperator/display/display.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <assert.h>

namespace {

SemaphoreHandle_t transferDone = nullptr;
esp_lcd_panel_io_handle_t registeredPanelIo = nullptr;

bool OnColorTransferDone(esp_lcd_panel_io_handle_t,
                         esp_lcd_panel_io_event_data_t *, void *userContext) {
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  if (userContext != nullptr) {
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(userContext),
                          &higherPriorityTaskWoken);
  }
  return higherPriorityTaskWoken == pdTRUE;
}

void ClearStaleCompletion() {
  if (transferDone == nullptr)
    return;
  while (xSemaphoreTake(transferDone, 0) == pdTRUE) {
  }
}

void WaitForCompletion() {
  if (transferDone == nullptr)
    return;
  configASSERT(xSemaphoreTake(transferDone, portMAX_DELAY) == pdTRUE);
}

} // namespace

extern "C" void display_rgb565_transport_init(void) {
  esp_lcd_panel_io_handle_t panelIo = NullperatorHAL::Display::GetPanelIO();
  if (panelIo == nullptr || registeredPanelIo == panelIo)
    return;

  if (transferDone == nullptr) {
    transferDone = xSemaphoreCreateBinary();
    assert(transferDone != nullptr);
  }

  const esp_lcd_panel_io_callbacks_t callbacks = {
      .on_color_trans_done = OnColorTransferDone,
  };
  ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(
      panelIo, &callbacks, transferDone));
  registeredPanelIo = panelIo;
}

extern "C" bool display_draw_rgb565_region(uint16_t x, uint16_t y,
                                            uint16_t width, uint16_t height,
                                            const uint16_t *pixels) {
  constexpr uint16_t DisplayWidth = 240;
  constexpr uint16_t DisplayHeight = 240;
  if (pixels == nullptr || width == 0 || height == 0 || x >= DisplayWidth ||
      y >= DisplayHeight || width > DisplayWidth - x ||
      height > DisplayHeight - y) {
    return false;
  }

  esp_lcd_panel_handle_t panel = NullperatorHAL::Display::GetPanel();
  if (panel == nullptr)
    return false;

  display_rgb565_transport_init();
  ClearStaleCompletion();
  const esp_err_t result = esp_lcd_panel_draw_bitmap(
      panel, x, y, x + width, y + height, pixels);
  if (result != ESP_OK)
    return false;
  WaitForCompletion();
  return true;
}
