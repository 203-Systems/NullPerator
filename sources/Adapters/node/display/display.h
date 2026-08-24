#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define TEXT_WIDTH 30
#define TEXT_HEIGHT 24

// ARNE-16 palette converted to RGB565
typedef enum {
  COLOR_BG,
  COLOR_NORMAL,
  COLOR_HILITE,
  COLOR_HILITE2,
  COLOR_GRAY,
  COLOR_DESERT,
  COLOR_ORANGE,
  COLOR_YELLOW,
  COLOR_WHITE,
  COLOR_MIDNIGHT,
  COLOR_DARK_SLATE_GRAY,
  COLOR_GREEN,
  COLOR_YELLOW_GREEN,
  COLOR_BLUE,
  COLOR_GURU_TXT,
  COLOR_GURU_BG
} color_t;

void display_init(void);
void display_set_font_index(uint8_t idx);
void display_set_cursor(uint8_t x, uint8_t y);
void display_putc(char c, bool invert);
void display_print(const char *s, bool invert);
void display_set_foreground(color_t color);
void display_set_background(color_t color);
void display_clear(color_t color);
void display_set_palette_color(int idx, uint16_t rgb565_color);
void display_draw_changed();
void display_draw_screen();
void display_fill_rect(uint8_t color_index, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height);
// Draws a synchronous RGB565 block. Pixels must already be in the byte order
// expected by esp_lcd (most-significant byte first on the ST7789 transport).
// The source remains owned by the caller and may be reused when this returns.
bool display_draw_rgb565_region(uint16_t x, uint16_t y, uint16_t width,
                                uint16_t height, const uint16_t *pixels);
void display_draw_region(uint8_t x, uint8_t y, uint8_t width, uint8_t height);
void display_draw_sub_region(uint8_t x, uint8_t y, uint8_t width,
                             uint8_t height);

#ifdef __cplusplus
}
#endif
