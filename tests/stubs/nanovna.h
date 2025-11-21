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
#include <stdint.h>

#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif

#define __VNA_USE_MATH_TABLES__ 1
#define __USE_VNA_MATH__ 1
#define SWEEP_POINTS_MAX 512
#define FFT_SIZE 512
#define VNA_PI 3.14159265358979323846f

typedef float audio_sample_t;
typedef uint32_t freq_t;

typedef float complex_sample_t[2];

#define SWAP(type, x, y)                                                                         \
  do {                                                                                           \
    type _tmp = (x);                                                                             \
    (x) = (y);                                                                                   \
    (y) = _tmp;                                                                                  \
  } while (0)

#include "processing/vna_math.h"
