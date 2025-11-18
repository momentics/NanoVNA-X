#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct display_presenter display_presenter_t;

typedef struct {
  void (*fill)(void* context, int x, int y, int w, int h);
  void (*bulk)(void* context, int x, int y, int w, int h);
  void (*drawchar)(void* context, uint8_t ch, int x, int y);
  int (*drawchar_size)(void* context, uint8_t ch, int x, int y, uint8_t size);
  void (*drawfont)(void* context, uint8_t ch, int x, int y);
  void (*drawstring)(void* context, int16_t x, int16_t y, const char* str);
  void (*drawstring_size)(void* context, const char* str, int x, int y, uint8_t size);
  int (*vprintf)(void* context, int16_t x, int16_t y, const char* fmt, va_list args);
  void (*read_memory)(void* context, int x, int y, int w, int h, uint16_t* out);
  void (*line)(void* context, int x0, int y0, int x1, int y1);
  void (*set_background)(void* context, uint16_t bg);
  void (*set_colors)(void* context, uint16_t fg, uint16_t bg);
  void (*set_flip)(void* context, bool flip);
  void (*set_font)(void* context, int type);
  void (*blit_bitmap)(void* context, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                      const uint8_t* bitmap);
} display_presenter_api_t;

struct display_presenter {
  void* context;
  const display_presenter_api_t* api;
};

void display_presenter_bind(const display_presenter_t* presenter);

void display_presenter_fill(int x, int y, int w, int h);
void display_presenter_bulk(int x, int y, int w, int h);
void display_presenter_drawchar(uint8_t ch, int x, int y);
int display_presenter_drawchar_size(uint8_t ch, int x, int y, uint8_t size);
void display_presenter_drawfont(uint8_t ch, int x, int y);
void display_presenter_drawstring(int16_t x, int16_t y, const char* str);
void display_presenter_drawstring_size(const char* str, int x, int y, uint8_t size);
int display_presenter_printf(int16_t x, int16_t y, const char* fmt, ...);
void display_presenter_read_memory(int x, int y, int w, int h, uint16_t* out);
void display_presenter_line(int x0, int y0, int x1, int y1);
void display_presenter_set_background(uint16_t bg);
void display_presenter_set_colors(uint16_t fg, uint16_t bg);
void display_presenter_set_flip(bool flip);
void display_presenter_set_font(int type);
void display_presenter_blit_bitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                   const uint8_t* bitmap);

extern const display_presenter_api_t display_presenter_lcd_api;
