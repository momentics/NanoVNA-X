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

#pragma once

#include <math.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include "arm_intrinsics.h"

#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif

#define __VNA_USE_MATH_TABLES__ 1
#define __USE_VNA_MATH__ 1
#define SWEEP_POINTS_MAX 512
#define FFT_SIZE 512
#define AUDIO_SAMPLES_COUNT 48
#define AUDIO_ADC_FREQ 192000
#define FREQUENCY_OFFSET (7000 * (AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT / 1000))
#define VNA_PI 3.14159265358979323846f

typedef float audio_sample_t;
typedef uint32_t freq_t;

typedef float complex_sample_t[2];

typedef struct {
  uint16_t _vna_mode;
  uint32_t _serial_speed;
} config_t;

extern config_t config;

enum { VNA_MODE_CONNECTION = 0 };

#define VNA_MODE(idx) (config._vna_mode & (1U << (idx)))

#define ARRAY_COUNT(a) (sizeof(a) / sizeof(*(a)))
#define STR1(x) #x
#define define_to_STR(x) STR1(x)

#define SWAP(type, x, y)                                                                         \
  do {                                                                                           \
    type _tmp = (x);                                                                             \
    (x) = (y);                                                                                   \
    (y) = _tmp;                                                                                  \
  } while (0)

#include "processing/vna_math.h"

int parse_line(char* line, char* args[], int max_cnt);
int get_str_index(const char* value, const char* list);

void lcd_fill(int x, int y, int w, int h);
void lcd_bulk(int x, int y, int w, int h);
void lcd_drawchar(uint8_t ch, int x, int y);
int lcd_drawchar_size(uint8_t ch, int x, int y, uint8_t size);
void lcd_drawfont(uint8_t ch, int x, int y);
void lcd_drawstring(int x, int y, const char* str);
void lcd_drawstring_size(const char* str, int x, int y, uint8_t size);
int lcd_printf_va(int16_t x, int16_t y, const char* fmt, va_list args);
void lcd_read_memory(int x, int y, int w, int h, uint16_t* out);
void lcd_line(int x0, int y0, int x1, int y1);
void lcd_set_background(uint16_t bg);
void lcd_set_colors(uint16_t fg, uint16_t bg);
void lcd_set_flip(bool flip);
void lcd_set_font(int type);
void lcd_blit_bitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t* bitmap);
