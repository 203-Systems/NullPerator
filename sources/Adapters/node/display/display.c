#include "display.h"
#include "Adapters/node/hal/nullperator/display/display.h"
#include "esp_lcd_panel_ops.h"
#include "font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

/* Character graphics mode */

#define TEXT_WIDTH 30
#define TEXT_HEIGHT 24
#define CHAR_HEIGHT 10
#define CHAR_WIDTH 8
#define BUFFER_CHARS 10

#define SWAP_BYTES(color) ((uint16_t)(color >> 8) | (uint16_t)(color << 8))

static color_t screen_bg_color = COLOR_BG;
static color_t screen_fg_color = COLOR_NORMAL;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t screen[TEXT_HEIGHT * TEXT_WIDTH] = {0};
static uint8_t colors[TEXT_HEIGHT * TEXT_WIDTH] = {0};
static uint16_t buffer[CHAR_WIDTH * BUFFER_CHARS * CHAR_HEIGHT] = {0};

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

void display_init(void) {}

void display_clear(color_t color) {
  int size = TEXT_WIDTH * TEXT_HEIGHT;
  memset(screen, 0, size);
  memset(colors, color, size);
  display_set_cursor(0, 0);
  display_draw_screen();
}

void display_set_foreground(color_t color) { screen_fg_color = color; }

void display_set_background(color_t color) { screen_bg_color = color; }

void display_set_font_index(uint8_t idx) { ui_font_index = idx; }

void display_set_cursor(uint8_t x, uint8_t y) {
  cursor_x = x;
  cursor_y = y;
}

void display_putc(char c, bool invert) {
  int idx = cursor_y * TEXT_WIDTH + cursor_x;
  if (c >= 32 && c <= 127) {
    screen[idx] = c - 32;
    SetBit(changed, idx);
    if (invert) {
      colors[idx] = ((screen_bg_color & 0xf) << 4) | (screen_fg_color & 0xf);
    } else {
      colors[idx] = ((screen_fg_color & 0xf) << 4) | (screen_bg_color & 0xf);
    }
  }
}

void display_print(const char *str, bool invert) {
  char c;
  while ((c = *str++)) {
    display_putc(c, invert);
  }
}

void display_draw_region(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
  int remainder = height;
  while (remainder) {
    int sub_height = (remainder > BUFFER_CHARS) ? BUFFER_CHARS : remainder;
    int sub_y = y + height - remainder;
    remainder -= sub_height;
    display_draw_sub_region(x, sub_y, width, sub_height);
  }
}

inline void display_draw_sub_region(uint8_t x, uint8_t y, uint8_t width,
                                    uint8_t height) {
  assert(height <= BUFFER_CHARS);

  const uint16_t screen_x = x * CHAR_WIDTH;
  const uint16_t screen_y = y * CHAR_HEIGHT;
  const uint16_t screen_height = height * CHAR_HEIGHT;

  esp_lcd_panel_handle_t panel = NullperatorHAL::Display::GetPanel();
  if (panel == NULL) {
    return;
  }

  for (uint8_t char_x = 0; char_x < width; ++char_x) {
    uint16_t *buffer_idx = buffer;

    for (uint8_t char_y = 0; char_y < height; ++char_y) {
      for (uint8_t pixel_y = 0; pixel_y < CHAR_HEIGHT; ++pixel_y) {
        const int idx = (y + char_y) * TEXT_WIDTH + (x + char_x);
        const uint8_t character = screen[idx];
        const uint16_t fg_color = palette[colors[idx] >> 4];
        const uint16_t bg_color = palette[colors[idx] & 0xf];
        const uint16_t *pixel_data =
            (ui_font_index == 0) ? FONT_STEALTH57_BITMAP[character]
                                 : FONT_YOU_SQUARED_BITMAP[character];
        const uint16_t row_bits = pixel_data[pixel_y];

        for (uint8_t pixel_x = 0; pixel_x < CHAR_WIDTH; ++pixel_x) {
          const uint16_t mask = static_cast<uint16_t>(1U << pixel_x);
          *buffer_idx++ = (row_bits & mask) ? fg_color : bg_color;
        }
      }
    }

    const uint16_t column_x = screen_x + (char_x * CHAR_WIDTH);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, column_x, screen_y,
                                              column_x + CHAR_WIDTH,
                                              screen_y + screen_height, buffer));
  }
}

void display_draw_changed() {
  for (int idx = 0; idx < TEXT_HEIGHT * TEXT_WIDTH; idx++) {
    if (TestBit(changed, idx)) {
      ClearBit(changed, idx);
      uint16_t y = idx / TEXT_WIDTH;
      uint16_t x = idx - (TEXT_WIDTH * y);

      int height = 1;
      for (int probe_y = y + 1; probe_y < TEXT_HEIGHT; probe_y++) {
        int probe_idx = probe_y * TEXT_WIDTH + x;
        if (TestBit(changed, probe_idx)) {
          ClearBit(changed, probe_idx);
          height++;
          continue;
        }
        break;
      }

      int16_t width = 1;
      for (int probe_x = x + 1; probe_x < TEXT_WIDTH; probe_x++) {
        for (int probe_y = y; probe_y < y + height; probe_y++) {
          int probe_idx = probe_y * TEXT_WIDTH + probe_x;
          if (!TestBit(changed, probe_idx)) {
            for (int undo_y = y; undo_y < probe_y; undo_y++) {
              SetBit(changed, undo_y * TEXT_WIDTH + probe_x);
            }
            goto end;
          }
          ClearBit(changed, probe_idx);
        }
        width++;
      }
    end:
      display_draw_region(x, y, width, height);
    }
  }
}

void display_draw_screen() { display_draw_region(0, 0, TEXT_WIDTH, TEXT_HEIGHT); }

void display_set_palette_color(int idx, uint16_t rgb565_color) {
  palette[idx] = SWAP_BYTES(rgb565_color);
}

void display_fill_rect(uint8_t color_index, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height) {
  if (width == 0 || height == 0) {
    return;
  }

  const size_t pixels = (size_t)width * height;
  uint16_t *fill_buffer = (uint16_t *)malloc(pixels * sizeof(uint16_t));
  if (fill_buffer == NULL) {
    return;
  }

  const uint16_t color = palette[color_index & 0x0F];
  for (size_t i = 0; i < pixels; ++i) {
    fill_buffer[i] = color;
  }

  esp_lcd_panel_handle_t panel = NullperatorHAL::Display::GetPanel();
  if (panel != NULL) {
    ESP_ERROR_CHECK(
        esp_lcd_panel_draw_bitmap(panel, x, y, x + width, y + height, fill_buffer));
  }
  free(fill_buffer);
}
