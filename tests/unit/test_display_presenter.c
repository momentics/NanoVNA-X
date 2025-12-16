/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/*
 * Host-side coverage for src/ui/display/display_presenter.c.
 *
 * The presenter API is intentionally thin, forwarding every call to whichever
 * implementation is currently bound.  These tests bind a mock presenter and
 * assert that coordinates, strings, and variadic arguments flow through
 * untouched while also verifying that NULL bindings simply no-op.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nanovna.h"
#include "ui/display/display_presenter.h"

config_t config = {0};

/* Stub out the LCD primitives referenced by display_presenter.c. */
void lcd_fill(int x, int y, int w, int h) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
}
void lcd_bulk(int x, int y, int w, int h) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
}
void lcd_drawchar(uint8_t ch, int x, int y) {
  (void)ch;
  (void)x;
  (void)y;
}
int lcd_drawchar_size(uint8_t ch, int x, int y, uint8_t size) {
  (void)ch;
  (void)x;
  (void)y;
  (void)size;
  return 0;
}
void lcd_drawfont(uint8_t ch, int x, int y) {
  (void)ch;
  (void)x;
  (void)y;
}
void lcd_drawstring(int x, int y, const char* str) {
  (void)x;
  (void)y;
  (void)str;
}
void lcd_drawstring_size(const char* str, int x, int y, uint8_t size) {
  (void)str;
  (void)x;
  (void)y;
  (void)size;
}
int lcd_printf_va(int16_t x, int16_t y, const char* fmt, va_list args) {
  (void)x;
  (void)y;
  char buffer[64];
  int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  return len;
}
void lcd_read_memory(int x, int y, int w, int h, uint16_t* out) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)out;
}
void lcd_line(int x0, int y0, int x1, int y1) {
  (void)x0;
  (void)y0;
  (void)x1;
  (void)y1;
}
void lcd_set_background(uint16_t bg) {
  (void)bg;
}
void lcd_set_colors(uint16_t fg, uint16_t bg) {
  (void)fg;
  (void)bg;
}
void lcd_set_flip(bool flip) {
  (void)flip;
}
void lcd_set_font(int type) {
  (void)type;
}
void lcd_blit_bitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     const uint8_t* bitmap) {
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)bitmap;
}

/* ------------------------------------------------------------------------- */

typedef struct {
  void* context_seen;
  int fill_calls;
  int drawstring_calls;
  int set_colors_calls;
  int last_fill[4];
  int last_string_x;
  int last_string_y;
  char last_string[32];
  uint16_t last_fg;
  uint16_t last_bg;
  int vprintf_calls;
  int last_printf_x;
  int last_printf_y;
  char last_printf_fmt[32];
  char last_printf_buf[32];
} mock_presenter_state_t;

static mock_presenter_state_t g_mock_state;

static void mock_fill(void* ctx, int x, int y, int w, int h) {
  g_mock_state.context_seen = ctx;
  ++g_mock_state.fill_calls;
  g_mock_state.last_fill[0] = x;
  g_mock_state.last_fill[1] = y;
  g_mock_state.last_fill[2] = w;
  g_mock_state.last_fill[3] = h;
}

static void mock_drawstring(void* ctx, int16_t x, int16_t y, const char* str) {
  g_mock_state.context_seen = ctx;
  ++g_mock_state.drawstring_calls;
  g_mock_state.last_string_x = x;
  g_mock_state.last_string_y = y;
  strncpy(g_mock_state.last_string, str, sizeof(g_mock_state.last_string) - 1);
}

static void mock_set_colors(void* ctx, uint16_t fg, uint16_t bg) {
  g_mock_state.context_seen = ctx;
  ++g_mock_state.set_colors_calls;
  g_mock_state.last_fg = fg;
  g_mock_state.last_bg = bg;
}

static int mock_vprintf(void* ctx, int16_t x, int16_t y, const char* fmt, va_list args) {
  g_mock_state.context_seen = ctx;
  ++g_mock_state.vprintf_calls;
  g_mock_state.last_printf_x = x;
  g_mock_state.last_printf_y = y;
  strncpy(g_mock_state.last_printf_fmt, fmt, sizeof(g_mock_state.last_printf_fmt) - 1);
  vsnprintf(g_mock_state.last_printf_buf, sizeof(g_mock_state.last_printf_buf), fmt, args);
  return (int)strlen(g_mock_state.last_printf_buf);
}

static const display_presenter_api_t g_mock_api = {
    .fill = mock_fill,
    .drawstring = mock_drawstring,
    .set_colors = mock_set_colors,
    .vprintf = mock_vprintf,
};

static int g_failures = 0;

#define CHECK(cond, msg)                                                                         \
  do {                                                                                           \
    if (!(cond)) {                                                                               \
      ++g_failures;                                                                              \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg);                            \
    }                                                                                            \
  } while (0)

static void test_presenter_forwards_calls(void) {
  memset(&g_mock_state, 0, sizeof(g_mock_state));
  int context = 42;
  display_presenter_t presenter = {.context = &context, .api = &g_mock_api};
  display_presenter_bind(&presenter);

  display_presenter_fill(1, 2, 3, 4);
  display_presenter_drawstring(5, 6, "HELLO");
  display_presenter_set_colors(7, 8);
  int printed = display_presenter_printf(9, 10, "%s %d", "ctx", 7);

  CHECK(g_mock_state.fill_calls == 1, "fill should be forwarded");
  CHECK(g_mock_state.last_fill[0] == 1 && g_mock_state.last_fill[3] == 4,
        "fill coordinates should match arguments");
  CHECK(g_mock_state.drawstring_calls == 1, "drawstring should be forwarded");
  CHECK(strcmp(g_mock_state.last_string, "HELLO") == 0, "drawstring text must match");
  CHECK(g_mock_state.set_colors_calls == 1, "set_colors should be forwarded");
  CHECK(g_mock_state.last_fg == 7 && g_mock_state.last_bg == 8, "colors should match arguments");
  CHECK(g_mock_state.vprintf_calls == 1, "printf should use the API vprintf");
  CHECK(strcmp(g_mock_state.last_printf_buf, "ctx 7") == 0, "printf must format via vprintf");
  CHECK(printed == (int)strlen("ctx 7"), "printf return must match API result");
  CHECK(g_mock_state.context_seen == &context, "context pointer must be relayed unchanged");
}

static void test_null_presenter_is_noop(void) {
  memset(&g_mock_state, 0, sizeof(g_mock_state));
  display_presenter_bind(NULL);
  display_presenter_fill(0, 0, 0, 0);
  display_presenter_drawstring(0, 0, "ignored");
  display_presenter_set_colors(0, 0);
  int rc = display_presenter_drawchar_size('A', 0, 0, 1);
  CHECK(g_mock_state.fill_calls == 0 && g_mock_state.drawstring_calls == 0,
        "no API calls must happen when presenter is NULL");
  CHECK(rc == 0, "drawchar_size should return 0 when API is missing");
}

int main(void) {
  test_presenter_forwards_calls();
  test_null_presenter_is_noop();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_display_presenter");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
