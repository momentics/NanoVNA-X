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

// 1. Configuration (defines VNA_PI, etc.)
#include "processing/dsp_config.h"

// 2. Data Types (defines properties_t, freq_t, __USE_VNA_MATH__ via config_macros)
#include "core/data_types.h"

// 3. Test overrides
#undef USE_VARIABLE_OFFSET
// #warning "Using STUB nanovna.h"
#define FFT_SIZE 512

// 4. Globals
extern properties_t current_props;
extern config_t config;

#define velocity_factor     current_props._velocity_factor
#define markers             current_props._markers
#define active_marker       0 // Mock active marker as 0

// 5. Helpers
#define VNA_MODE(idx) (config._vna_mode & (1U << (idx)))
enum { VNA_MODE_CONNECTION = 0 };

#ifndef STR1
#define STR1(x) #x
#endif
#ifndef define_to_STR
#define define_to_STR(x) STR1(x)
#endif
#define ARRAY_COUNT(a) (sizeof(a) / sizeof(*(a)))

#define SWAP(type, x, y)                                                                         \
  do {                                                                                           \
    type _tmp = (x);                                                                             \
    (x) = (y);                                                                                   \
    (y) = _tmp;                                                                                  \
  } while (0)

// 6. UI Mocks
#define FONT_STR_HEIGHT      8
#define STR_MEASURE_HEIGHT (FONT_STR_HEIGHT + 1)
#define FONT_WIDTH           6
#define STR_MEASURE_WIDTH  (FONT_WIDTH * 10)
#define OFFSETX                      10
#define OFFSETY                       0
#define STR_MEASURE_X      (OFFSETX +  0)
#define STR_MEASURE_Y      (OFFSETY + 80)

#define S_OHM      "\x1E"
#define S_METRE    "m"
#define S_dB       "dB"
#define S_Hz       "Hz"
#define S_FARAD    "F"
#define S_HENRY    "H"
#define S_DELTA    "\x17"
#define CELLHEIGHT 10

#define PORT_Z 50.0f
// 7. Measure Flags
#define MEASURE_UPD_SWEEP (1 << 0)
#define MEASURE_UPD_FREQ  (1 << 2)
#define MEASURE_UPD_ALL   (MEASURE_UPD_SWEEP | MEASURE_UPD_FREQ)

// 8. Prototypes
void invalidate_rect(int x, int y, int w, int h);
void cell_printf(int x, int y, const char* fmt, ...);
float resistance(int i, const float* v);
float reactance(int i, const float* v);
float swr(int i, const float* v);
float logmag(int i, const float* v);
freq_t get_marker_frequency(int marker);
void markmap_all_markers(void);
void pause_sweep(void);

int parse_line(char* line, char* args[], int max_cnt);
int get_str_index(const char* value, const char* list);

// 9. VNA Math (must be after VNA_PI and __USE_VNA_MATH__ are available)
#include "processing/vna_math.h"

// 10. More UI prototypes
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
