/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
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
#include "nanovna.h"
#include <stdint.h>

// Use table increase transform speed, but increase code size
// Use compact table, need 1/4 code size, and not decrease speed
// Used only if not defined VNA_USE_MATH_TABLES (use self table for TTF or direct sin/cos
// calculations)
#define FFT_USE_SIN_COS_TABLE

// Use sin table and interpolation for sin/sos calculations
#ifdef VNA_USE_MATH_TABLES

// Platform-specific quarter-wave table configuration
#ifdef NANOVNA_F303
// F303: Use larger quarter-wave table for improved accuracy (0 to 90 degrees)
#define QTR_WAVE_TABLE_SIZE 1024  // 1023 entries for 0-90 degrees + 1 for interpolation
#define FAST_MATH_TABLE_SIZE 1024 // Keep for compatibility with FFT_SIZE calculations
#else
// F072: Use smaller quarter-wave table balancing memory usage vs accuracy (0 to 90 degrees)
#define QTR_WAVE_TABLE_SIZE 257   // 256 entries for 0-90 degrees + 1 for interpolation
#define FAST_MATH_TABLE_SIZE 1024 // Keep for compatibility with FFT_SIZE calculations
#endif

// Include platform-specific sin table
#include "sin_tables.h"

#ifdef NANOVNA_F303
// Use F303-specific table
static const float *sin_table_qtr = SIN_TABLE_QTR_F303;
#else
// Use F072-specific table
static const float *sin_table_qtr = SIN_TABLE_QTR_F072;
#endif
//
// Common function for quadratic interpolation using the quarter-wave table
static inline float quadratic_interpolation(float x) {
  int idx = (int)x;
  float fract = x - idx;

  // Handle negative indices by wrapping or clamping to 0
  if (idx < 0) {
    idx = 0;
    fract = 0.0f; // For negative values, just return value at index 0
  }

  // Clamp to prevent out-of-bounds access
  if (idx >= QTR_WAVE_TABLE_SIZE - 1) {
    // At or beyond the last index, return the last value
    return sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
  }

  float y0, y1, y2;
  if (idx >= QTR_WAVE_TABLE_SIZE - 2) {
    // At or near the end of table, use linear interpolation to avoid out-of-bounds
    y0 = sin_table_qtr[idx];
    y1 = sin_table_qtr[idx + 1];
    y2 = y1; // Use same value for quadratic computation (effectively linear)
  } else {
    y0 = sin_table_qtr[idx];
    y1 = sin_table_qtr[idx + 1];
    y2 = sin_table_qtr[idx + 2];
  }

  if (idx >= QTR_WAVE_TABLE_SIZE - 2) {
    // Use linear interpolation near boundaries
    float t = (idx >= QTR_WAVE_TABLE_SIZE - 1) ? 1.0f : fract;
    return y0 + t * (y1 - y0);
  } else {
    // Use quadratic interpolation: f(x) = f0 + t*(f1-f0) + t*(t-1)*(f2-2*f1+f0)/2
    return y0 + fract * (y1 - y0) + fract * (fract - 1.0f) * (y2 - 2.0f * y1 + y0) * 0.5f;
  }
}

// Define the quarter-wave table size for mapping FFT calculations
#ifdef NANOVNA_F303
#define QTR_WAVE_TABLE_SIZE_FOR_CALC 1023.0f // Actual size - 1 for interpolation (1024 points)
#else
#define QTR_WAVE_TABLE_SIZE_FOR_CALC 256.0f // Actual size - 1 for interpolation (257 points)
#endif

#if FFT_SIZE == 256
// For FFT_SIZE = 256, table index maps to angle (i/256)*360 degrees
// Using quarter-wave table (0-90 degrees) with variable intervals depending on platform, we need to
// map accordingly
#if !defined(VNA_USE_MATH_TABLES) || defined(NANOVNA_HOST_TEST)
static inline float fft_sin_256(uint16_t i) {
  float angle = (2.0f * VNA_PI * i) / 256.0f;
  return sinf(angle);
}

static inline float fft_cos_256(uint16_t i) {
  float angle = (2.0f * VNA_PI * i) / 256.0f;
  return cosf(angle);
}
#else
static inline float fft_sin_256(uint16_t i) {
  // i ranges from 0 to 255, representing angles from 0 to 358.59... degrees
  uint8_t quad = i >> 6;          // i / 64 = quadrant (0-3)
  uint8_t in_quad_pos = i & 0x3F; // i % 64 = position within quadrant (0-63)

  // Each FFT quadrant (64 steps) represents 90 degrees
  // Our table has QTR_WAVE_TABLE_SIZE_FOR_CALC intervals for 90 degrees, so each FFT step
  // corresponds to QTR_WAVE_TABLE_SIZE_FOR_CALC/64 table steps
  float table_float_idx = in_quad_pos * (QTR_WAVE_TABLE_SIZE_FOR_CALC / 64.0f);

  // Get sin value using common quadratic interpolation function
  float sin_interp = quadratic_interpolation(table_float_idx);

  // Calculate complementary angle for cosine (cos(x) = sin(90° - x))
  float comp_float_idx = (QTR_WAVE_TABLE_SIZE_FOR_CALC - table_float_idx);
  float cos_interp;
  if (comp_float_idx >= QTR_WAVE_TABLE_SIZE_FOR_CALC) {
    // When cos_scaled is at or above table size, directly use the value at last index (sin of 90° =
    // 1)
    cos_interp = sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
  } else if (comp_float_idx < 0.0f) {
    // When cos_scaled is below 0, directly use the value at index 0 (sin of 0° = 0)
    cos_interp = sin_table_qtr[0];
  } else {
    // Get cosine value using common quadratic interpolation function
    cos_interp = quadratic_interpolation(comp_float_idx);
  }

  // Apply quadrant-specific transformations
  if (quad == 0) { // 0-90 degrees
    return sin_interp;
  } else if (quad == 1) { // 90-180 degrees: sin(90°+x) = cos(x)
    return cos_interp;
  } else if (quad == 2) { // 180-270 degrees: sin(180°+x) = -sin(x)
    return -sin_interp;
  } else { // 270-360 degrees: sin(270°+x) = -cos(x)
    return -cos_interp;
  }
}

static inline float fft_cos_256(uint16_t i) {
  uint8_t quad = i >> 6;          // i / 64 = quadrant (0-3)
  uint8_t in_quad_pos = i & 0x3F; // i % 64 = position within quadrant (0-63)

  // Each FFT quadrant (64 steps) represents 90 degrees
  // Our table has QTR_WAVE_TABLE_SIZE_FOR_CALC intervals for 90 degrees, so each FFT step
  // corresponds to QTR_WAVE_TABLE_SIZE_FOR_CALC/64 table steps
  float table_float_idx = in_quad_pos * (QTR_WAVE_TABLE_SIZE_FOR_CALC / 64.0f);

  // Get sin value using common quadratic interpolation function
  float sin_interp = quadratic_interpolation(table_float_idx);

  // Calculate complementary angle for cosine (cos(x) = sin(90° - x))
  float comp_float_idx = (QTR_WAVE_TABLE_SIZE_FOR_CALC - table_float_idx);
  float cos_interp;
  if (comp_float_idx >= QTR_WAVE_TABLE_SIZE_FOR_CALC) {
    // When cos_scaled is at or above table size, directly use the value at last index (sin of 90° =
    // 1)
    cos_interp = sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
  } else if (comp_float_idx < 0.0f) {
    // When cos_scaled is below 0, directly use the value at index 0 (sin of 0° = 0)
    cos_interp = sin_table_qtr[0];
  } else {
    // Get cosine value using common quadratic interpolation function
    cos_interp = quadratic_interpolation(comp_float_idx);
  }

  // Apply quadrant-specific transformations
  if (quad == 0) { // 0-90 degrees: cos(x) -> sin(90° - x)
    return cos_interp;
  } else if (quad == 1) { // 90-180 degrees: cos(90°+x) = -sin(x)
    return -sin_interp;
  } else if (quad == 2) { // 180-270 degrees: cos(180°+x) = -cos(x)
    return -cos_interp;
  } else { // 270-360 degrees: cos(270°+x) = sin(x)
    return sin_interp;
  }
}
#endif // !defined(VNA_USE_MATH_TABLES) || defined(NANOVNA_HOST_TEST)

#define FFT_SIN(i) fft_sin_256(i)
#define FFT_COS(i) fft_cos_256(i)
#elif FFT_SIZE == 512
// For FFT_SIZE = 512, table index maps to angle (i/512)*360 degrees
// Using quarter-wave table (0-90 degrees) with variable intervals depending on platform, we need to
// map accordingly
#if !defined(VNA_USE_MATH_TABLES) || defined(NANOVNA_HOST_TEST)
static inline float fft_sin_512(uint16_t i) {
  float angle = (2.0f * VNA_PI * i) / 512.0f;
  return sinf(angle);
}
static inline float fft_cos_512(uint16_t i) {
  float angle = (2.0f * VNA_PI * i) / 512.0f;
  return cosf(angle);
}
#else
static inline float fft_sin_512(uint16_t i) {
  // i ranges from 0 to 511, representing angles from 0 to 359.29... degrees
  // For 512-point FFT, each step is 360/512 degrees
  uint8_t quad = i >> 7;          // i / 128 = quadrant (0-3), 512/4 = 128 steps per quadrant
  uint8_t in_quad_pos = i & 0x7F; // i % 128 = position within quadrant (0-127)

  // Each FFT quadrant (128 steps) represents 90 degrees
  // Our table has QTR_WAVE_TABLE_SIZE_FOR_CALC intervals for 90 degrees, so each FFT step
  // corresponds to QTR_WAVE_TABLE_SIZE_FOR_CALC/128 table steps
  float table_float_idx = in_quad_pos * (QTR_WAVE_TABLE_SIZE_FOR_CALC / 128.0f);

  // Get sin value using common quadratic interpolation function
  float sin_interp = quadratic_interpolation(table_float_idx);

  // Calculate complementary angle for cosine
  float comp_float_idx = (QTR_WAVE_TABLE_SIZE_FOR_CALC - table_float_idx);
  float cos_interp;
  if (comp_float_idx >= QTR_WAVE_TABLE_SIZE_FOR_CALC) {
    // When cos_scaled is at or above table size, directly use the value at last index (sin of 90° =
    // 1)
    cos_interp = sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
  } else if (comp_float_idx < 0.0f) {
    // When cos_scaled is below 0, directly use the value at index 0 (sin of 0° = 0)
    cos_interp = sin_table_qtr[0];
  } else {
    // Get cosine value using common quadratic interpolation function
    cos_interp = quadratic_interpolation(comp_float_idx);
  }

  // Apply quadrant-specific transformations
  if (quad == 0) { // 0-90 degrees
    return sin_interp;
  } else if (quad == 1) { // 90-180 degrees: sin(90°+x) = cos(x)
    return cos_interp;
  } else if (quad == 2) { // 180-270 degrees: sin(180°+x) = -sin(x)
    return -sin_interp;
  } else { // 270-360 degrees: sin(270°+x) = -cos(x)
    return -cos_interp;
  }
}

static inline float fft_cos_512(uint16_t i) {
  uint8_t quad = i >> 7;          // i / 128 = quadrant (0-3)
  uint8_t in_quad_pos = i & 0x7F; // i % 128 = position within quadrant (0-127)

  // Each FFT quadrant (128 steps) represents 90 degrees
  // Our table has QTR_WAVE_TABLE_SIZE_FOR_CALC intervals for 90 degrees, so each FFT step
  // corresponds to QTR_WAVE_TABLE_SIZE_FOR_CALC/128 table steps
  float table_float_idx = in_quad_pos * (QTR_WAVE_TABLE_SIZE_FOR_CALC / 128.0f);

  // Get sin value using common quadratic interpolation function
  float sin_interp = quadratic_interpolation(table_float_idx);

  // Calculate complementary angle for cosine
  float comp_float_idx = (QTR_WAVE_TABLE_SIZE_FOR_CALC - table_float_idx);
  float cos_interp;
  if (comp_float_idx >= QTR_WAVE_TABLE_SIZE_FOR_CALC) {
    // When cos_scaled is at or above table size, directly use the value at last index (sin of 90° =
    // 1)
    cos_interp = sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
  } else if (comp_float_idx < 0.0f) {
    // When cos_scaled is below 0, directly use the value at index 0 (sin of 0° = 0)
    cos_interp = sin_table_qtr[0];
  } else {
    // Get cosine value using common quadratic interpolation function
    cos_interp = quadratic_interpolation(comp_float_idx);
  }

  // Apply quadrant-specific transformations
  if (quad == 0) { // 0-90 degrees: cos(x) -> sin(90° - x)
    return cos_interp;
  } else if (quad == 1) { // 90-180 degrees: cos(90°+x) = -sin(x)
    return -sin_interp;
  } else if (quad == 2) { // 180-270 degrees: cos(180°+x) = -cos(x)
    return -cos_interp;
  } else { // 270-360 degrees: cos(270°+x) = sin(x)
    return sin_interp;
  }
}
#endif // !defined(VNA_USE_MATH_TABLES) || defined(NANOVNA_HOST_TEST)

#define FFT_SIN(i) fft_sin_512(i)
#define FFT_COS(i) fft_cos_512(i)
#else
#error "Need use bigger sin/cos table for new FFT size"
#endif

// Clean up the temporary macro
#undef QTR_WAVE_TABLE_SIZE_FOR_CALC

#endif // VNA_USE_MATH_TABLES

#ifdef ARM_MATH_CM4
// Use CORTEX M4 rbit instruction (reverse bit order in 32bit value)
static uint32_t reverse_bits(uint32_t x, int n) {
  uint32_t result;
  __asm volatile("rbit %0, %1" : "=r"(result) : "r"(x));
  return result >> (32 - n); // made shift for correct result
}
#else
static uint16_t reverse_bits(uint16_t x, int n) {
  uint16_t result = 0;
  int i;
  for (i = 0; i < n; i++, x >>= 1)
    result = (result << 1) | (x & 1U);
  return result;
}
#endif

/***
 * dir = forward: 0, inverse: 1
 * https://www.nayuki.io/res/free-small-fft-in-multiple-languages/fft.c
 */
void fft(float array[][2], const uint8_t dir) {
// FFT_SIZE = 2^FFT_N
#if FFT_SIZE == 256
#define FFT_N 8
#elif FFT_SIZE == 512
#define FFT_N 9
#else
#error "Need define FFT_N for this FFT size"
#endif
  const uint16_t n = FFT_SIZE;
  const uint8_t levels = FFT_N; // log2(n)
  uint16_t i, j;
  for (i = 0; i < n; i++) {
    if ((j = reverse_bits(i, levels)) > i) {
      SWAP(float, array[i][0], array[j][0]);
      SWAP(float, array[i][1], array[j][1]);
    }
  }
  const uint16_t size = 2;
  uint16_t halfsize = size / 2;
  uint16_t tablestep = n / size;
  // Cooley-Tukey decimation-in-time radix-2 FFT
  for (; tablestep; tablestep >>= 1, halfsize <<= 1) {
    for (i = 0; i < n; i += halfsize * 2) { // Fixed: corrected outer loop increment
      for (j = 0; j < halfsize; j++) {      // Fixed: removed i++ from j loop
        const uint16_t k = i + j;
        const uint16_t l = k + halfsize;
        const uint16_t w_index = j * tablestep;
        const float s = dir ? FFT_SIN(w_index) : -FFT_SIN(w_index);
        const float c = FFT_COS(w_index);
        const float tpre = array[l][0] * c - array[l][1] * s;
        const float tpim = array[l][0] * s + array[l][1] * c;
        array[l][0] = array[k][0] - tpre;
        array[k][0] += tpre;
        array[l][1] = array[k][1] - tpim;
        array[k][1] += tpim;
      }
    }
  }
}

// Return sin/cos value angle in range 0.0 to 1.0 (0 is 0 degree, 1 is 360 degree)
void vna_sincosf(float angle, float *p_sin_val, float *p_cos_val) {
#if !defined(VNA_USE_MATH_TABLES) || defined(NANOVNA_HOST_TEST)
  // Use default sin/cos functions for host tests
  angle *= 2.0f * VNA_PI; // Convert to rad
  *p_sin_val = sinf(angle);
  *p_cos_val = cosf(angle);
#else
  // Normalize angle to range [0, 1) using modff to handle negative values correctly
  float fpart, ipart;
  fpart = vna_modff(angle, &ipart); // Get fractional part in [-1, 1) range
  if (fpart < 0.0f) {
    fpart += 1.0f; // Convert to [0, 1) range
  }

  // Define platform-specific parameters for table mapping
#ifdef NANOVNA_F303
  const float table_size_per_quarter = 1023.0f; // QTR_WAVE_TABLE_SIZE - 1 for F303
  const float table_size_full_circle = 4092.0f; // 4 * table_size_per_quarter
#else
  const float table_size_per_quarter = 256.0f;  // QTR_WAVE_TABLE_SIZE - 1 for F072
  const float table_size_full_circle = 1024.0f; // 4 * table_size_per_quarter
#endif

  // Scale to map to our quarter-wave table covering full 360 degree circle
  // Our quarter table covers 90 degrees with variable number of entries depending on platform
  // So full circle needs 4 * table_size_per_quarter entries
  float scaled = fpart * table_size_full_circle;
  uint16_t full_index = (uint16_t)scaled;
  float fract = scaled - full_index;

  // Determine quadrant (0-3 for 4 quadrants of 90 degrees each)
  uint8_t quad = full_index / (uint8_t)table_size_per_quarter;          // which quadrant (0-3)
  uint16_t in_quad_pos = full_index % (uint16_t)table_size_per_quarter; // position within quadrant

  // Get sin value using common quadratic interpolation function
  float sin_interp = quadratic_interpolation(in_quad_pos + fract);

  // For cosine, we need sin at complementary angle: cos(x) = sin(90° - x)
  // Calculate complementary angle in table index space
  float comp_angle = table_size_per_quarter - (in_quad_pos + fract);
  float cos_interp;
  if (comp_angle >= table_size_per_quarter) {
    // When comp_angle is at or above table size per quarter, directly use the value at last index
    // (sin of 90° = 1)
    cos_interp = sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
  } else if (comp_angle < 0.0f) {
    // When comp_angle is below 0, directly use the value at index 0 (sin of 0° = 0)
    cos_interp = sin_table_qtr[0];
  } else {
    // Get cosine value using common quadratic interpolation function
    cos_interp = quadratic_interpolation(comp_angle);
  }

  float sin_final, cos_final;

  // Apply quadrant-specific transformations using correct trigonometric identities
  switch (quad) {
  case 0: // 0 to 90 degrees (first quadrant): 0.000 to 1/table_size_full_circle
    // Angle maps directly to [0°, 90°]
    sin_final = sin_interp; // sin(x)
    cos_final = cos_interp; // cos(x)
    break;
  case 1: // 90 to 180 degrees (second quadrant)
    // For angle in [90°, 180°], let x = actual_angle - 90°, where x ∈ [0°, 90°]
    // sin(90° + x) = cos(x), cos(90° + x) = -sin(x)
    sin_final = cos_interp;  // cos(x) where x is the equivalent angle in [0°,90°]
    cos_final = -sin_interp; // -sin(x) where x is the equivalent angle in [0°,90°]
    break;
  case 2: // 180 to 270 degrees (third quadrant)
    // For angle in [180°, 270°], let x = actual_angle - 180°, where x ∈ [0°, 90°]
    // sin(180° + x) = -sin(x), cos(180° + x) = -cos(x)
    sin_final = -sin_interp; // -sin(x) where x is the equivalent angle in [0°,90°]
    cos_final = -cos_interp; // -cos(x) where x is the equivalent angle in [0°,90°]
    break;
  case 3: // 270 to 360 degrees (fourth quadrant)
    // For angle in [270°, 360°], let x = actual_angle - 270°, where x ∈ [0°, 90°]
    // sin(270° + x) = -cos(x), cos(270° + x) = sin(x)
    sin_final = -cos_interp; // -cos(x) where x is the equivalent angle in [0°,90°]
    cos_final = sin_interp;  // sin(x) where x is the equivalent angle in [0°,90°]
    break;
  default:
    sin_final = 0.0f;
    cos_final = 1.0f;
    break;
  }

  *p_sin_val = sin_final;
  *p_cos_val = cos_final;
#endif
}

//**********************************************************************************
//      VNA math
//**********************************************************************************
// Cleanup declarations if used default math.h functions
#undef vna_sqrtf
#undef vna_cbrtf
#undef vna_logf
#undef vna_atanf
#undef vna_atan2f
#undef vna_modff

//**********************************************************************************
// modff function - return fractional part and integer from float value x
//**********************************************************************************
float vna_modff(float x, float *iptr) {
  union {
    float f;
    uint32_t i;
  } u = {x};
  int e = (int)((u.i >> 23) & 0xff) - 0x7f; // get exponent
  if (e < 0) {                              // no integral part
    if (iptr)
      *iptr = 0;
    return u.f;
  }
  if (e >= 23) {
    x = 0; // no fractional part
  } else {
    x = u.f;
    u.i &= ~(0x007fffff >> e); // remove fractional part from u
    x -= u.f;                  // calc fractional part
  }
  // if (iptr) *iptr = ((u.i&0x007fffff)|0x00800000)>>(23-e); // cut integer part from float as
  // integer
  if (iptr)
    *iptr = u.f; // cut integer part from float as float
  return x;
}

//**********************************************************************************
// square root
//**********************************************************************************
#if (__FPU_PRESENT == 0) && (__FPU_USED == 0)
#if 1
// __ieee754_sqrtf, remove some check (NAN, inf, normalization), small code optimization to arm
float vna_sqrtf(float x) {
  int32_t ix, s, q, m, t;
  uint32_t r;
  union {
    float f;
    uint32_t i;
  } u = {x};
  ix = u.i;
#if 0
  // take care of Inf and NaN
  if((ix&0x7f800000)==0x7f800000) return x*x+x;	// sqrt(NaN)=NaN, sqrt(+inf)=+inf, sqrt(-inf)=sNaN
  // take care if x < 0
  if (ix <  0) return (x-x)/0.0f;
#endif
  if (ix == 0)
    return 0.0f;
  m = (ix >> 23);
#if 0 //
  // normalize x
  if(m==0) {				// subnormal x
    for(int i=0;(ix&0x00800000)==0;i++) ix<<=1;
      m -= i-1;
  }
#endif
  m -= 127; // unbias exponent
  ix = (ix & 0x007fffff) | 0x00800000;
  // generate sqrt(x) bit by bit
  ix <<= (m & 1) ? 2 : 1; // odd m, double x to make it even, and after multiple by 2
  m >>= 1;                // m = [m/2]
  q = s = 0;              // q = sqrt(x)
  r = 0x01000000;         // r = moving bit from right to left
  while (r != 0) {
    t = s + r;
    if (t <= ix) {
      s = t + r;
      ix -= t;
      q += r;
    }
    ix += ix;
    r >>= 1;
  }
  // use floating add to find out rounding direction
  if (ix != 0) {
    if ((1.0f - 1e-30f) >= 1.0f) // trigger inexact flag.
      q += ((1.0f + 1e-30f) > 1.0f) ? 2 : (q & 1);
  }
  ix = (q >> 1) + 0x3f000000;
  ix += (m << 23);
  u.i = ix;
  return u.f;
}
#else
// Simple implementation, but slow if no FPU used, and not usable if used hardware FPU sqrtf
float vna_sqrtf(float x) {
  union {
    float x;
    uint32_t i;
  } u = {x};
  u.i = (1 << 29) + (u.i >> 1) - (1 << 22);
  // Two Babylonian Steps (simplified from:)
  // u.x = 0.5f * (u.x + x/u.x);
  // u.x = 0.5f * (u.x + x/u.x);
  u.x = u.x + x / u.x;
  u.x = 0.25f * u.x + x / u.x;

  return u.x;
}
#endif
#endif

//**********************************************************************************
// Cube root
//**********************************************************************************
float vna_cbrtf(float x) {
#if 1
  static const uint32_t b1 = 709958130, // B1 = (127-127.0/3-0.03306235651)*2**23
    b2 = 642849266;                     // B2 = (127-127.0/3-24/3-0.03306235651)*2**23

  float r, t;
  union {
    float f;
    uint32_t i;
  } u = {x};
  uint32_t hx = u.i & 0x7fffffff;

  //	if (hx >= 0x7f800000)  // cbrt(NaN,INF) is itself
  //		return x + x;
  // rough cbrtf to 5 bits
  if (hx < 0x00800000) { // zero or subnormal?
    if (hx == 0)
      return x; // cbrt(+-0) is itself
    u.f = x * 0x1p24f;
    hx = u.i & 0x7fffffff;
    hx = hx / 3 + b2;
  } else
    hx = hx / 3 + b1;
  u.i &= 0x80000000;
  u.i |= hx;

  // First step Newton iteration (solving t*t-x/t == 0) to 16 bits.
  t = u.f;
  r = t * t * t;
  t *= (x + x + r) / (x + r + r);
  // Second step Newton iteration to 47 bits.
  r = t * t * t;
  t *= (x + x + r) / (x + r + r);
  return t;
#else
  if (x == 0) {
    // would otherwise return something like 4.257959840008151e-109
    return 0;
  }
  float b = 1.0f; // use any value except 0
  float last_b_1 = 0;
  float last_b_2 = 0;
  while (last_b_1 != b && last_b_2 != b) {
    last_b_1 = b;
    //    b = (b + x / (b * b)) / 2;
    b = (2 * b + x / b / b) / 3; // for small numbers, as suggested by  willywonka_dailyblah
    last_b_2 = b;
    //    b = (b + x / (b * b)) / 2;
    b = (2 * b + x / b / b) / 3; // for small numbers, as suggested by  willywonka_dailyblah
  }
  return b;
#endif
}

//**********************************************************************************
// logf
//**********************************************************************************
float vna_logf(float x) {
  const float multiplier = logf(2.0f);
#if 0
  // Give up to 0.006 error (2.5x faster original code)
  union {float f; int32_t i;} u = {x};
  const int      log_2 = ((u.i >> 23) & 255) - 128;
  if (u.i <=0) return -1/(x*x);                 // if <=0 return -inf
  u.i = (u.i&0x007FFFFF) + 0x3F800000;
  u.f = ((-1.0f/3) * u.f + 2) * u.f - (2.0f/3); // (1)
  return (u.f + log_2) * MULTIPLIER;
#elif 1
  // Give up to 0.00005 error (2x faster original code)
  // fast log2f approximation, give 0.0002 error
  union {
    float f;
    uint32_t i;
  } vx = {x};
  union {
    uint32_t i;
    float f;
  } mx = {(vx.i & 0x007FFFFF) | 0x3f000000};
  // if <=0 return NAN
  if (vx.i <= 0)
    return -1 / (x * x);
  return vx.i * (multiplier / (1 << 23)) - (124.22544637f * multiplier) -
         (1.498030302f * multiplier) * mx.f - (1.72587999f * multiplier) / (0.3520887068f + mx.f);
#else
  // use original code (20% faster default)
  static const float ln2_hi = 6.9313812256e-01, /* 0x3f317180 */
    ln2_lo = 9.0580006145e-06,                  /* 0x3717f7d1 */
    two25 = 3.355443200e+07,                    /* 0x4c000000 */
    /* |(log(1+s)-log(1-s))/s - Lg(s)| < 2**-34.24 (~[-4.95e-11, 4.97e-11]). */
    Lg1 = 0xaaaaaa.0p-24, /* 0.66666662693 */
    Lg2 = 0xccce13.0p-25, /* 0.40000972152 */
    Lg3 = 0x91e9ee.0p-25, /* 0.28498786688 */
    Lg4 = 0xf89e26.0p-26; /* 0.24279078841 */

  union {
    float f;
    uint32_t i;
  } u = {x};
  float hfsq, f, s, z, R, w, t1, t2, dk;
  uint32_t ix;
  int k;

  ix = u.i;
  k = 0;
  if (ix < 0x00800000 || ix >> 31) { /* x < 2**-126  */
    if (ix << 1 == 0)
      return -1 / (x * x); /* log(+-0)=-inf */
    if (ix >> 31)
      return (x - x) / 0.0f; /* log(-#) = NaN */
    /* subnormal number, scale up x */
    k -= 25;
    x *= two25;
    u.f = x;
    ix = u.i;
  } else if (ix >= 0x7f800000) {
    return x;
  } else if (ix == 0x3f800000)
    return 0;
  /* reduce x into [sqrt(2)/2, sqrt(2)] */
  ix += 0x3f800000 - 0x3f3504f3;
  k += (int)(ix >> 23) - 0x7f;
  ix = (ix & 0x007fffff) + 0x3f3504f3;
  u.i = ix;
  x = u.f;
  f = x - 1.0f;
  s = f / (2.0f + f);
  z = s * s;
  w = z * z;
  t1 = w * (Lg2 + w * Lg4);
  t2 = z * (Lg1 + w * Lg3);
  R = t2 + t1;
  hfsq = 0.5f * f * f;
  dk = k;
  return s * (hfsq + R) + dk * ln2_lo - hfsq + f + dk * ln2_hi;
#endif
}

float vna_log10f_x_10(float x) {
  const float multiplier = (10.0f * logf(2.0f) / logf(10.0f));
#if 0
  // Give up to 0.006 error (2.5x faster original code)
  union {float f; int32_t i;} u = {x};
  const int      log_2 = ((u.i >> 23) & 255) - 128;
  if (u.i <=0) return -1/(x*x);                 // if <=0 return -inf
  u.i = (u.i&0x007FFFFF) + 0x3F800000;
  u.f = ((-1.0f/3) * u.f + 2) * u.f - (2.0f/3); // (1)
  return (u.f + log_2) * MULTIPLIER;
#else
  // Give up to 0.0001 error (2x faster original code)
  // fast log2f approximation, give 0.0004 error
  union {
    float f;
    uint32_t i;
  } vx = {x};
  union {
    uint32_t i;
    float f;
  } mx = {(vx.i & 0x007FFFFF) | 0x3f000000};
  // if <=0 return NAN
  if (vx.i <= 0)
    return -1 / (x * x);
  return vx.i * (multiplier / (1 << 23)) - (124.22544637f * multiplier) -
         (1.498030302f * multiplier) * mx.f - (1.72587999f * multiplier) / (0.3520887068f + mx.f);
#endif
}
//**********************************************************************************
// atanf
//**********************************************************************************
// __ieee754_atanf
float vna_atanf(float x) {
  static const float atanhi[] = {
    4.6364760399e-01, // atan(0.5)hi 0x3eed6338
    7.8539812565e-01, // atan(1.0)hi 0x3f490fda
    9.8279368877e-01, // atan(1.5)hi 0x3f7b985e
    1.5707962513e+00, // atan(inf)hi 0x3fc90fda
  };
  static const float atanlo[] = {
    5.0121582440e-09, // atan(0.5)lo 0x31ac3769
    3.7748947079e-08, // atan(1.0)lo 0x33222168
    3.4473217170e-08, // atan(1.5)lo 0x33140fb4
    7.5497894159e-08, // atan(inf)lo 0x33a22168
  };
  static const float a_t[] = {
    3.3333328366e-01, -1.9999158382e-01, 1.4253635705e-01, -1.0648017377e-01, 6.1687607318e-02,
  };
  float w, s1, s2, z;
  uint32_t ix, sign;
  int id;
  union {
    float f;
    uint32_t i;
  } u = {x};
  ix = u.i;
  sign = ix >> 31;
  ix &= 0x7fffffff;
  if (ix >= 0x4c800000) { /* if |x| >= 2**26 */
    if (ix > 0x7f800000)
      return x;
    z = atanhi[3] + 0x1p-120f;
    return sign ? -z : z;
  }
  if (ix < 0x3ee00000) {   /* |x| < 0.4375 */
    if (ix < 0x39800000) { /* |x| < 2**-12 */
      return x;
    }
    id = -1;
  } else {
    x = vna_fabsf(x);
    if (ix < 0x3f980000) {   /* |x| < 1.1875 */
      if (ix < 0x3f300000) { /*  7/16 <= |x| < 11/16 */
        id = 0;
        x = (2.0f * x - 1.0f) / (2.0f + x);
      } else { /* 11/16 <= |x| < 19/16 */
        id = 1;
        x = (x - 1.0f) / (x + 1.0f);
      }
    } else {
      if (ix < 0x401c0000) { /* |x| < 2.4375 */
        id = 2;
        x = (x - 1.5f) / (1.0f + 1.5f * x);
      } else { /* 2.4375 <= |x| < 2**26 */
        id = 3;
        x = -1.0f / x;
      }
    }
  }
  /* end of argument reduction */
  z = x * x;
  w = z * z;
  /* break sum from i=0 to 10 aT[i]z**(i+1) into odd and even poly */
  s1 = z * (a_t[0] + w * (a_t[2] + w * a_t[4]));
  s2 = w * (a_t[1] + w * a_t[3]);
  if (id < 0)
    return x - x * (s1 + s2);
  z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
  return sign ? -z : z;
}

//**********************************************************************************
// atan2f
//**********************************************************************************
#if 0
// __ieee754_atan2f
float vna_atan2f(float y, float x)
{
  static const float pi    = 3.1415927410e+00; // 0x40490fdb
  static const float pi_lo =-8.7422776573e-08; // 0xb3bbbd2e
  float z;
  uint32_t m,ix,iy;
  union {float f; uint32_t i;} ux = {x};
  union {float f; uint32_t i;} uy = {y};
  ix = ux.i;
  iy = uy.i;

  if (ix == 0x3f800000)  /* x=1.0 */
    return vna_atanf(y);
  m = ((iy>>31)&1) | ((ix>>30)&2);  /* 2*sign(x)+sign(y) */
  ix &= 0x7fffffff;
  iy &= 0x7fffffff;

  /* when y = 0 */
  if (iy == 0) {
	switch (m) {
      case 0:
      case 1: return   y; // atan(+-0,+anything)=+-0
      case 2: return  pi; // atan(+0,-anything) = pi
      case 3: return -pi; // atan(-0,-anything) =-pi
    }
  }
  /* when x = 0 */
  if (ix == 0)
    return m&1 ? -pi/2 : pi/2;
  /* when x is INF */
  if (ix == 0x7f800000) {
    if (iy == 0x7f800000) {
      switch (m) {
        case 0: return  pi/4; /* atan(+INF,+INF) */
        case 1: return -pi/4; /* atan(-INF,+INF) */
        case 2: return 3*pi/4;  /*atan(+INF,-INF)*/
        case 3: return -3*pi/4; /*atan(-INF,-INF)*/
      }
    } else {
      switch (m) {
        case 0: return  0.0f;    /* atan(+...,+INF) */
        case 1: return -0.0f;    /* atan(-...,+INF) */
        case 2: return  pi; /* atan(+...,-INF) */
        case 3: return -pi; /* atan(-...,-INF) */
      }
    }
  }
  /* |y/x| > 0x1p26 */
  if (ix+(26<<23) < iy || iy == 0x7f800000)
    return m&1 ? -pi/2 : pi/2;

  /* z = atan(|y/x|) with correct underflow */
  if ((m&2) && iy+(26<<23) < ix)  /*|y/x| < 0x1p-26, x < 0 */
    z = 0.0;
  else
    z = vna_atanf(vna_fabsf(y/x));
  switch (m) {
    case 0: return z;              /* atan(+,+) */
    case 1: return -z;             /* atan(-,+) */
    case 2: return pi - (z-pi_lo); /* atan(+,-) */
    default: /* case 3 */
      return (z-pi_lo) - pi; /* atan(-,-) */
  }
}
#else
// Polynomial approximation to atan2f
float vna_atan2f(float y, float x) {
  union {
    float f;
    int32_t i;
  } ux = {x};
  union {
    float f;
    int32_t i;
  } uy = {y};
  if (ux.i == 0 && uy.i == 0)
    return 0.0f;

  float ax, ay, r, s;
  ax = vna_fabsf(x);
  ay = vna_fabsf(y);
  r = (ay < ax) ? ay / ax : ax / ay;
  s = r * r;
  // Polynomial approximation to atan(a) on [0,1]
#if 0
  // give 0.31 degree error
  r*= 0.970562748477141f - 0.189514164974601f * s;
  //r*= vna_fmaf(-s, 0.189514164974601f, 0.970562748477141f);
#elif 0
  // give 0.04 degree error
  r *= 0.994949366116654f - s * (0.287060635532652f - 0.078037176446441f * s);
  // r*= vna_fmaf(-s, vna_fmaf(-s, 0.078037176446441f, 0.287060635532652f), 0.994949366116654f);
  // r*= 0.995354f − s * (0.288679f + 0.079331f * s);
#else
  // give 0.005 degree error
  r *= 0.999133448222780f -
       s * (0.320533292381664f - s * (0.144982490144465f - s * 0.038254464970299f));
  // r*= vna_fmaf(-s, vna_fmaf(-s, vna_fmaf(-s, 0.038254464970299f, 0.144982490144465f),
  // 0.320533292381664f), 0.999133448222780f);
#endif
  // Map to full circle
  if (ay > ax)
    r = VNA_PI / 2.0f - r;
  if (ux.i < 0)
    r = VNA_PI - r;
  if (uy.i < 0)
    r = -r;
  return r;
}
#endif

//**********************************************************************************
// Fast expf approximation
//**********************************************************************************
float vna_expf(float x) {
  union {
    float f;
    int32_t i;
  } v;
  v.i = (int32_t)(12102203.0f * x) + 0x3F800000;
  int32_t m = (v.i >> 7) & 0xFFFF; // copy mantissa
#if 1
  // cubic spline approximation, empirical values for small maximum relative error (8.34e-5):
  v.i += ((((((((1277 * m) >> 14) + 14825) * m) >> 14) - 79749) * m) >> 11) - 626;
#else
  // quartic spline approximation, empirical values for small maximum relative error (1.21e-5):
  v.i += (((((((((((3537 * m) >> 16) + 13668) * m) >> 18) + 15817) * m) >> 14) - 80470) * m) >> 11);
#endif
  return v.f;
}
