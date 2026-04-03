#include "display.h"
#include "Adapters/node/hal/nullperator/display/display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "font.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Character graphics mode */

#define DISPLAY_WIDTH (TEXT_WIDTH * CHAR_WIDTH)
#define DISPLAY_HEIGHT (TEXT_HEIGHT * CHAR_HEIGHT)
#define ROW_BUFFER_PIXELS (DISPLAY_WIDTH * CHAR_HEIGHT)
#define FILL_CHUNK_ROWS 8

#define SWAP_BYTES(color) ((uint16_t)(color >> 8) | (uint16_t)(color << 8))

static color_t screen_bg_color = COLOR_BG;
static color_t screen_fg_color = COLOR_NORMAL;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t screen[TEXT_HEIGHT * TEXT_WIDTH] = {0};
static uint8_t colors[TEXT_HEIGHT * TEXT_WIDTH] = {0};
static uint16_t row_buffer[ROW_BUFFER_PIXELS] = {0};
static uint16_t fill_buffer[DISPLAY_WIDTH * FILL_CHUNK_ROWS] = {0};

static uint8_t ui_font_index = 0;

static uint8_t changed[TEXT_HEIGHT * TEXT_WIDTH / 8] = {0};
#define SetBit(A, k) (A[(k) / 8] |= (1 << ((k) % 8)))
#define ClearBit(A, k) (A[(k) / 8] &= ~(1 << ((k) % 8)))
#define TestBit(A, k) (A[(k) / 8] & (1 << ((k) % 8)))

static uint16_t palette[16] = {
    SWAP_BYTES(0x0000), SWAP_BYTES(0x49E5), SWAP_BYTES(0xB926),
    SWAP_BYTES(0xE371), SWAP_BYTES(0x9CF3), SWAP_BYTES(0xA324),
    SWAP_BYTES(0xEC46), SWAP_BYTES(0xF70D), SWAP_BYTES(0xffff),
    SWAP_BYTES(0x1926), SWAP_BYTES(0x2A49), SWAP_BYTES(0x4443),
    SWAP_BYTES(0xA664), SWAP_BYTES(0x02B0), SWAP_BYTES(0x351E),
    SWAP_BYTES(0xB6FD)};
static SemaphoreHandle_t transfer_done = NULL;
static esp_lcd_panel_io_handle_t registered_panel_io = NULL;

static const uint16_t *get_regular_glyph_bitmap(uint8_t character) {
  return (ui_font_index == 0) ? FONT_STEALTH57_BITMAP[character]
                              : FONT_YOU_SQUARED_BITMAP[character];
}

static const uint8_t *get_special_glyph_bitmap(uint8_t character) {
  const uint8_t codepoint = static_cast<uint8_t>(character + 32);
  return (codepoint < 0x80) ? nullptr : FONT_SPECIAL_CHARACTERS_BITMAP[codepoint - 0x80];
}

static uint16_t get_glyph_row_bits(uint8_t character, uint8_t pixel_y) {
  if (character < 96) {
    return get_regular_glyph_bitmap(character)[pixel_y];
  }

  if (const uint8_t *special = get_special_glyph_bitmap(character)) {
    return special[pixel_y];
  }

  return get_regular_glyph_bitmap(0)[pixel_y];
}

static bool on_color_transfer_done(esp_lcd_panel_io_handle_t,
                                   esp_lcd_panel_io_event_data_t *,
                                   void *user_ctx) {
  BaseType_t higher_priority_task_woken = pdFALSE;
  if (user_ctx != NULL) {
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx,
                          &higher_priority_task_woken);
  }
  return higher_priority_task_woken == pdTRUE;
}

static void ensure_panel_io_callback(void) {
  esp_lcd_panel_io_handle_t panel_io = NullperatorHAL::Display::GetPanelIO();
  if (panel_io == NULL || registered_panel_io == panel_io) {
    return;
  }

  if (transfer_done == NULL) {
    transfer_done = xSemaphoreCreateBinary();
    assert(transfer_done != NULL);
  }

  const esp_lcd_panel_io_callbacks_t callbacks = {
      .on_color_trans_done = on_color_transfer_done,
  };
  ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(panel_io, &callbacks,
                                                            transfer_done));
  registered_panel_io = panel_io;
}

static void wait_for_color_transfer_done(void) {
  if (transfer_done == NULL) {
    return;
  }
  configASSERT(xSemaphoreTake(transfer_done, portMAX_DELAY) == pdTRUE);
}

static void clear_stale_color_transfer_done(void) {
  if (transfer_done == NULL) {
    return;
  }

  while (xSemaphoreTake(transfer_done, 0) == pdTRUE) {
  }
}

static void mark_all_changed(void) { memset(changed, 0xFF, sizeof(changed)); }

static bool has_pending_changes(void) {
  for (size_t i = 0; i < sizeof(changed); ++i) {
    if (changed[i] != 0) {
      return true;
    }
  }
  return false;
}

static void render_text_span(uint8_t x, uint8_t y, uint8_t width) {
  assert(width > 0);

  esp_lcd_panel_handle_t panel = NullperatorHAL::Display::GetPanel();
  if (panel == NULL) {
    return;
  }
  ensure_panel_io_callback();

  const uint16_t screen_x = x * CHAR_WIDTH;
  const uint16_t screen_y = y * CHAR_HEIGHT;
  const uint16_t screen_width = width * CHAR_WIDTH;

  for (uint8_t pixel_y = 0; pixel_y < CHAR_HEIGHT; ++pixel_y) {
    uint16_t *row = row_buffer + (pixel_y * screen_width);

    for (uint8_t char_x = 0; char_x < width; ++char_x) {
      const int idx = (y * TEXT_WIDTH) + (x + char_x);
      const uint8_t character = screen[idx];
      const uint16_t fg_color = palette[colors[idx] >> 4];
      const uint16_t bg_color = palette[colors[idx] & 0x0F];
      const uint16_t row_bits = get_glyph_row_bits(character, pixel_y);
      uint16_t *glyph = row + (char_x * CHAR_WIDTH);

      for (uint8_t pixel_x = 0; pixel_x < CHAR_WIDTH; ++pixel_x) {
        const uint16_t mask = static_cast<uint16_t>(1U << pixel_x);
        glyph[pixel_x] = (row_bits & mask) ? fg_color : bg_color;
      }
    }
  }

  clear_stale_color_transfer_done();
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, screen_x, screen_y,
                                            screen_x + screen_width,
                                            screen_y + CHAR_HEIGHT, row_buffer));
  wait_for_color_transfer_done();
}

void display_init(void) { ensure_panel_io_callback(); }

void display_clear(color_t color) {
  int size = TEXT_WIDTH * TEXT_HEIGHT;
  memset(screen, 0, size);
  memset(colors, color, size);
  mark_all_changed();
  display_set_cursor(0, 0);
}

void display_set_foreground(color_t color) { screen_fg_color = color; }

void display_set_background(color_t color) { screen_bg_color = color; }

void display_set_font_index(uint8_t idx) {
  if (ui_font_index != idx) {
    ui_font_index = idx;
    mark_all_changed();
  }
}

void display_set_cursor(uint8_t x, uint8_t y) {
  cursor_x = (x < TEXT_WIDTH) ? x : (TEXT_WIDTH - 1);
  cursor_y = (y < TEXT_HEIGHT) ? y : (TEXT_HEIGHT - 1);
}

void display_putc(char c, bool invert) {
  if ((cursor_x < 0) || (cursor_x >= TEXT_WIDTH) || (cursor_y < 0) ||
      (cursor_y >= TEXT_HEIGHT)) {
    return;
  }

  int idx = cursor_y * TEXT_WIDTH + cursor_x;
  const uint8_t glyph = static_cast<uint8_t>(c);
  if (glyph < 32) {
    screen[idx] = 0;
  } else {
    screen[idx] = glyph - 32;
  }
  SetBit(changed, idx);
  if (invert) {
    colors[idx] = ((screen_bg_color & 0xf) << 4) | (screen_fg_color & 0xf);
  } else {
    colors[idx] = ((screen_fg_color & 0xf) << 4) | (screen_bg_color & 0xf);
  }
}

void display_print(const char *str, bool invert) {
  char c;
  while ((c = *str++)) {
    if (cursor_x >= TEXT_WIDTH) {
      cursor_x = 0;
      if (cursor_y < (TEXT_HEIGHT - 1)) {
        cursor_y++;
      }
    }
    display_putc(c, invert);
    if (cursor_x < (TEXT_WIDTH - 1)) {
      cursor_x++;
    } else {
      cursor_x = TEXT_WIDTH;
    }
  }
}

void display_draw_region(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
  if (width == 0 || height == 0) {
    return;
  }

  for (uint8_t row = 0; row < height; ++row) {
    display_draw_sub_region(x, y + row, width, 1);
  }
}

inline void display_draw_sub_region(uint8_t x, uint8_t y, uint8_t width,
                                    uint8_t height) {
  if (width == 0 || height == 0) {
    return;
  }

  for (uint8_t row = 0; row < height; ++row) {
    render_text_span(x, y + row, width);
  }
}

void display_draw_changed() {
  if (!has_pending_changes()) {
    return;
  }

  for (uint8_t y = 0; y < TEXT_HEIGHT; ++y) {
    uint8_t x = 0;
    while (x < TEXT_WIDTH) {
      const int start_idx = (y * TEXT_WIDTH) + x;
      if (!TestBit(changed, start_idx)) {
        ++x;
        continue;
      }

      uint8_t run_width = 1;
      while ((x + run_width) < TEXT_WIDTH &&
             TestBit(changed, start_idx + run_width)) {
        ++run_width;
      }

      render_text_span(x, y, run_width);
      for (uint8_t i = 0; i < run_width; ++i) {
        ClearBit(changed, start_idx + i);
      }
      x += run_width;
    }
  }
}

void display_draw_screen() {
  mark_all_changed();
  display_draw_changed();
}

void display_set_palette_color(int idx, uint16_t rgb565_color) {
  const uint16_t new_color = SWAP_BYTES(rgb565_color);
  if (palette[idx] != new_color) {
    palette[idx] = new_color;
    mark_all_changed();
  }
}

void display_fill_rect(uint8_t color_index, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height) {
  if (width == 0 || height == 0) {
    return;
  }

  const uint16_t color = palette[color_index & 0x0F];
  esp_lcd_panel_handle_t panel = NullperatorHAL::Display::GetPanel();
  if (panel == NULL) {
    return;
  }
  ensure_panel_io_callback();

  const uint16_t chunk_width =
      (width > DISPLAY_WIDTH) ? DISPLAY_WIDTH : static_cast<uint16_t>(width);
  const size_t chunk_pixels = (size_t)chunk_width * FILL_CHUNK_ROWS;
  for (size_t i = 0; i < chunk_pixels; ++i) {
    fill_buffer[i] = color;
  }

  uint16_t remaining = height;
  uint16_t draw_y = y;
  while (remaining > 0) {
    const uint16_t chunk_rows =
        (remaining > FILL_CHUNK_ROWS) ? FILL_CHUNK_ROWS : remaining;
    const size_t row_pixels = (size_t)width * chunk_rows;
    for (size_t i = 0; i < row_pixels; ++i) {
      fill_buffer[i] = color;
    }

    clear_stale_color_transfer_done();
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, x, draw_y, x + width,
                                              draw_y + chunk_rows, fill_buffer));
    wait_for_color_transfer_done();
    draw_y += chunk_rows;
    remaining -= chunk_rows;
  }
}
