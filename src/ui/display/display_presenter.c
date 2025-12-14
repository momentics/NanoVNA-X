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

#include "ui/display/display_presenter.h"

#include "nanovna.h"

static const display_presenter_t *active_presenter = NULL;

void display_presenter_bind(const display_presenter_t *presenter) {
  active_presenter = presenter;
}

static inline const display_presenter_t *display_presenter_current(void) {
  return active_presenter;
}

static inline const display_presenter_api_t *display_presenter_current_api(void) {
  const display_presenter_t *presenter = display_presenter_current();
  return (presenter != NULL) ? presenter->api : NULL;
}

static inline void *display_presenter_context(void) {
  const display_presenter_t *presenter = display_presenter_current();
  return presenter ? presenter->context : NULL;
}

void display_presenter_fill(int x, int y, int w, int h) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->fill != NULL) {
    api->fill(display_presenter_context(), x, y, w, h);
  }
}

void display_presenter_bulk(int x, int y, int w, int h) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->bulk != NULL) {
    api->bulk(display_presenter_context(), x, y, w, h);
  }
}

void display_presenter_drawchar(uint8_t ch, int x, int y) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->drawchar != NULL) {
    api->drawchar(display_presenter_context(), ch, x, y);
  }
}

int display_presenter_drawchar_size(uint8_t ch, int x, int y, uint8_t size) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->drawchar_size != NULL) {
    return api->drawchar_size(display_presenter_context(), ch, x, y, size);
  }
  return 0;
}

void display_presenter_drawfont(uint8_t ch, int x, int y) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->drawfont != NULL) {
    api->drawfont(display_presenter_context(), ch, x, y);
  }
}

void display_presenter_drawstring(int16_t x, int16_t y, const char *str) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->drawstring != NULL) {
    api->drawstring(display_presenter_context(), x, y, str);
  }
}

void display_presenter_drawstring_size(const char *str, int x, int y, uint8_t size) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->drawstring_size != NULL) {
    api->drawstring_size(display_presenter_context(), str, x, y, size);
  }
}

int display_presenter_printf(int16_t x, int16_t y, const char *fmt, ...) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api == NULL || api->vprintf == NULL) {
    return 0;
  }
  va_list args;
  va_start(args, fmt);
  int result = api->vprintf(display_presenter_context(), x, y, fmt, args);
  va_end(args);
  return result;
}

void display_presenter_read_memory(int x, int y, int w, int h, uint16_t *out) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->read_memory != NULL) {
    api->read_memory(display_presenter_context(), x, y, w, h, out);
  }
}

void display_presenter_line(int x0, int y0, int x1, int y1) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->line != NULL) {
    api->line(display_presenter_context(), x0, y0, x1, y1);
  }
}

void display_presenter_set_background(uint16_t bg) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->set_background != NULL) {
    api->set_background(display_presenter_context(), bg);
  }
}

void display_presenter_set_colors(uint16_t fg, uint16_t bg) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->set_colors != NULL) {
    api->set_colors(display_presenter_context(), fg, bg);
  }
}

void display_presenter_set_flip(bool flip) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->set_flip != NULL) {
    api->set_flip(display_presenter_context(), flip);
  }
}

void display_presenter_set_font(int type) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->set_font != NULL) {
    api->set_font(display_presenter_context(), type);
  }
}

void display_presenter_blit_bitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                   const uint8_t *bitmap) {
  const display_presenter_api_t *api = display_presenter_current_api();
  if (api != NULL && api->blit_bitmap != NULL) {
    api->blit_bitmap(display_presenter_context(), x, y, width, height, bitmap);
  }
}

static void display_presenter_lcd_fill(void *context, int x, int y, int w, int h) {
  (void)context;
  lcd_fill(x, y, w, h);
}

static void display_presenter_lcd_bulk(void *context, int x, int y, int w, int h) {
  (void)context;
  lcd_bulk(x, y, w, h);
}

static void display_presenter_lcd_drawchar(void *context, uint8_t ch, int x, int y) {
  (void)context;
  lcd_drawchar(ch, x, y);
}

static int display_presenter_lcd_drawchar_size(void *context, uint8_t ch, int x, int y,
                                               uint8_t size) {
  (void)context;
  return lcd_drawchar_size(ch, x, y, size);
}

static void display_presenter_lcd_drawfont(void *context, uint8_t ch, int x, int y) {
  (void)context;
  lcd_drawfont(ch, x, y);
}

static void display_presenter_lcd_drawstring(void *context, int16_t x, int16_t y, const char *str) {
  (void)context;
  LCD_DRAWSTRING(x, y, str);
}

static void display_presenter_lcd_drawstring_size(void *context, const char *str, int x, int y,
                                                  uint8_t size) {
  (void)context;
  lcd_drawstring_size(str, x, y, size);
}

static int display_presenter_lcd_vprintf(void *context, int16_t x, int16_t y, const char *fmt,
                                         va_list args) {
  (void)context;
  return lcd_printf_va(x, y, fmt, args);
}

static void display_presenter_lcd_read_memory(void *context, int x, int y, int w, int h,
                                              uint16_t *out) {
  (void)context;
  lcd_read_memory(x, y, w, h, out);
}

static void display_presenter_lcd_line(void *context, int x0, int y0, int x1, int y1) {
  (void)context;
  lcd_line(x0, y0, x1, y1);
}

static void display_presenter_lcd_set_background(void *context, uint16_t bg) {
  (void)context;
  lcd_set_background(bg);
}

static void display_presenter_lcd_set_colors(void *context, uint16_t fg, uint16_t bg) {
  (void)context;
  lcd_set_colors(fg, bg);
}

static void display_presenter_lcd_set_flip(void *context, bool flip) {
  (void)context;
  lcd_set_flip(flip);
}

static void display_presenter_lcd_set_font(void *context, int type) {
  (void)context;
  LCD_SET_FONT(type);
}

static void display_presenter_lcd_blit_bitmap(void *context, uint16_t x, uint16_t y, uint16_t width,
                                              uint16_t height, const uint8_t *bitmap) {
  (void)context;
  lcd_blit_bitmap(x, y, width, height, bitmap);
}

const display_presenter_api_t DISPLAY_PRESENTER_LCD_API = {
  .fill = display_presenter_lcd_fill,
  .bulk = display_presenter_lcd_bulk,
  .drawchar = display_presenter_lcd_drawchar,
  .drawchar_size = display_presenter_lcd_drawchar_size,
  .drawfont = display_presenter_lcd_drawfont,
  .drawstring = display_presenter_lcd_drawstring,
  .drawstring_size = display_presenter_lcd_drawstring_size,
  .vprintf = display_presenter_lcd_vprintf,
  .read_memory = display_presenter_lcd_read_memory,
  .line = display_presenter_lcd_line,
  .set_background = display_presenter_lcd_set_background,
  .set_colors = display_presenter_lcd_set_colors,
  .set_flip = display_presenter_lcd_set_flip,
  .set_font = display_presenter_lcd_set_font,
  .blit_bitmap = display_presenter_lcd_blit_bitmap,
};
