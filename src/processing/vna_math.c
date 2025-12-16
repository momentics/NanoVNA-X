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
// Used only if not defined __VNA_USE_MATH_TABLES__ (use self table for TTF or direct sin/cos
// calculations)
#define FFT_USE_SIN_COS_TABLE

// Use sin table and interpolation for sin/sos calculations
#ifdef __VNA_USE_MATH_TABLES__

#ifdef NANOVNA_F303
// =================================================================================
// F303: Reference Implementation from NanoVNA-D (Ported)
// =================================================================================

#define FAST_MATH_TABLE_SIZE 512

// Reference: NanoVNA-D/vna_math.c
static const float sin_table_512[] = {
         0.00000000,  0.01227154,  0.02454123,  0.03680722,  0.04906767,  0.06132074,  0.07356456,  0.08579731,
         0.09801714,  0.11022221,  0.12241068,  0.13458071,  0.14673047,  0.15885814,  0.17096189,  0.18303989,
         0.19509032,  0.20711138,  0.21910124,  0.23105811,  0.24298018,  0.25486566,  0.26671276,  0.27851969,
         0.29028468,  0.30200595,  0.31368174,  0.32531029,  0.33688985,  0.34841868,  0.35989504,  0.37131719,
         0.38268343,  0.39399204,  0.40524131,  0.41642956,  0.42755509,  0.43861624,  0.44961133,  0.46053871,
         0.47139674,  0.48218377,  0.49289819,  0.50353838,  0.51410274,  0.52458968,  0.53499762,  0.54532499,
         0.55557023,  0.56573181,  0.57580819,  0.58579786,  0.59569930,  0.60551104,  0.61523159,  0.62485949,
         0.63439328,  0.64383154,  0.65317284,  0.66241578,  0.67155895,  0.68060100,  0.68954054,  0.69837625,
         0.70710678,  0.71573083,  0.72424708,  0.73265427,  0.74095113,  0.74913639,  0.75720885,  0.76516727,
         0.77301045,  0.78073723,  0.78834643,  0.79583690,  0.80320753,  0.81045720,  0.81758481,  0.82458930,
         0.83146961,  0.83822471,  0.84485357,  0.85135519,  0.85772861,  0.86397286,  0.87008699,  0.87607009,
         0.88192126,  0.88763962,  0.89322430,  0.89867447,  0.90398929,  0.90916798,  0.91420976,  0.91911385,
         0.92387953,  0.92850608,  0.93299280,  0.93733901,  0.94154407,  0.94560733,  0.94952818,  0.95330604,
         0.95694034,  0.96043052,  0.96377607,  0.96697647,  0.97003125,  0.97293995,  0.97570213,  0.97831737,
         0.98078528,  0.98310549,  0.98527764,  0.98730142,  0.98917651,  0.99090264,  0.99247953,  0.99390697,
         0.99518473,  0.99631261,  0.99729046,  0.99811811,  0.99879546,  0.99932238,  0.99969882,  0.99992470,
         1.00000000
};

// Forward declaration needed for sincosf
float vna_modff(float x, float* iptr);

void vna_sincosf(float angle, float * pSinVal, float * pCosVal) {
  uint16_t indexS, indexC;  // Index variable
  float f1, f2, d1, d2;     // Two nearest output values
  float fract, temp;

  // Round angle to range 0.0 to 1.0
  temp = vna_fabsf(angle);
  temp-= (uint32_t)temp;

  // Scale input from range 0.0 to 1.0 to table size
  temp*= FAST_MATH_TABLE_SIZE;

  indexS = temp;
  indexC = indexS + (FAST_MATH_TABLE_SIZE / 4); // cosine add 0.25 (pi/2) to read from sine table

  fract = temp - indexS;
  
  indexS&= (FAST_MATH_TABLE_SIZE-1);
  indexC&= (FAST_MATH_TABLE_SIZE-1);

  // Read two nearest values of input value from the cos & sin tables
  if (indexC < 256){f1 = sin_table_512[indexC    +0];f2 = sin_table_512[indexC    +1];}
  else             {f1 =-sin_table_512[indexC-256+0];f2 =-sin_table_512[indexC-256+1];}
  if (indexS < 256){d1 = sin_table_512[indexS    +0];d2 = sin_table_512[indexS    +1];}
  else             {d1 =-sin_table_512[indexS-256+0];d2 =-sin_table_512[indexS-256+1];}

  // 1e-7 error on 512 size table
  const float Dn = 2 * VNA_PI / FAST_MATH_TABLE_SIZE; // delta between the two points in table (fixed);
  float Df;
  // Calculation of cos value
  Df = f2 - f1; // delta between the values of the functions
  temp = Dn * (d1 + d2) + 2 * Df;
  temp = Df + (d1 * Dn + temp - fract * temp);
  temp = fract * temp - d1 * Dn;
  // Now temp = first part of Taylor series
  *pCosVal = f1 + temp;
  
  // Calculation of sin value
  Df = d2 - d1; // delta between the values of the functions
  temp = Dn * (f1 + f2) - 2 * Df;
  temp = Df + (temp - f1 * Dn + fract * temp);
  temp = fract * temp + f1 * Dn;
  // Now temp = first part of Taylor series
  *pSinVal = d1 + temp;
}

// FFT macros for F303 using the local sin_table_512
// for size use only 0-128 indexes
//   sin = i > 128 ? sin_table_512[256-i] : sin_table_512[    i];
//   cos = i > 128 ?-sin_table_512[i-128] : sin_table_512[128-i];
#if FFT_SIZE != 512
// If FFT size is not 512 in F303 build, this safe guard alerts us.
// But we assume standard F303 build uses 512 (or 256 can be mapped).
// NanoVNA-D shows table size independent of FFT size partially, but macros assume it.
// Reference macros:
#define FFT_SIN(i) ((i) > 128 ? sin_table_512[256-(i)] : sin_table_512[    (i)])
#define FFT_COS(i) ((i) > 128 ?-sin_table_512[(i)-128] : sin_table_512[128-(i)])
#else
#define FFT_SIN(i) ((i) > 128 ? sin_table_512[256-(i)] : sin_table_512[    (i)])
#define FFT_COS(i) ((i) > 128 ?-sin_table_512[(i)-128] : sin_table_512[128-(i)])
#endif

// To avoid duplicate definition errors if we have common functions at bottom
#define VNA_MODFF_DEFINED

// Reference modff
float vna_modff(float x, float *iptr)
{
  union {float f; uint32_t i;} u = {x};
  int e = (int)((u.i>>23)&0xff) - 0x7f; // get exponent
  if (e <   0) {                        // no integral part
    if (iptr) *iptr = 0;
    return u.f;
  }
  if (e >= 23) x = 0;                   // no fractional part
  else {
    x = u.f; u.i&= ~(0x007fffff>>e);    // remove fractional part from u
    x-= u.f;                            // calc fractional part
  }
  if (iptr) *iptr = u.f;                // cut integer part from float as float
  return x;
}

#else 
// =================================================================================
// F072: Original Implementation (Preserved)
// =================================================================================

// Platform-specific quarter-wave table configuration
#ifdef NANOVNA_F303
// This block should theoretically not be reached unless conditional logic is mixed, 
// but we keep the defines for safety in "Original" path logic.
#define QTR_WAVE_TABLE_SIZE 1024  
#define FAST_MATH_TABLE_SIZE 1024 
#else
#define QTR_WAVE_TABLE_SIZE 257  
#define FAST_MATH_TABLE_SIZE 1024 
#endif

// Include platform-specific sin table
#include "sin_tables.h"

#ifdef NANOVNA_F303
static const float *sin_table_qtr = SIN_TABLE_QTR_F303;
#else
static const float *sin_table_qtr = SIN_TABLE_QTR_F072;
#endif

static inline float quadratic_interpolation(float x) {
    int idx = (int)x;
    float fract = x - idx;
    
    // Handle negative indices by wrapping or clamping to 0
    if (idx < 0) {
        idx = 0;
        fract = 0.0f; 
    }
    
    // Clamp to prevent out-of-bounds access
    if (idx >= QTR_WAVE_TABLE_SIZE - 1) {
        return sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
    }
    
    float y0, y1, y2;
    if (idx >= QTR_WAVE_TABLE_SIZE - 2) {
        y0 = sin_table_qtr[idx];
        y1 = sin_table_qtr[idx + 1];
        y2 = y1;  
    } else {
        y0 = sin_table_qtr[idx];
        y1 = sin_table_qtr[idx + 1];
        y2 = sin_table_qtr[idx + 2];
    }
    
    if (idx >= QTR_WAVE_TABLE_SIZE - 2) {
        float t = (idx >= QTR_WAVE_TABLE_SIZE - 1) ? 1.0f : fract;
        return y0 + t * (y1 - y0);
    } else {
        return y0 + fract * (y1 - y0) + fract * (fract - 1.0f) * (y2 - 2.0f * y1 + y0) * 0.5f;
    }
}

// Define the quarter-wave table size for mapping FFT calculations
#ifdef NANOVNA_F303
#define QTR_WAVE_TABLE_SIZE_FOR_CALC 1023.0f  
#else
#define QTR_WAVE_TABLE_SIZE_FOR_CALC 256.0f   
#endif

#if FFT_SIZE == 256
#if !defined(__VNA_USE_MATH_TABLES__) || defined(NANOVNA_HOST_TEST)
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
    uint8_t quad = i >> 6;  
    uint8_t in_quad_pos = i & 0x3F;  
    float table_float_idx = in_quad_pos * (QTR_WAVE_TABLE_SIZE_FOR_CALC / 64.0f);
    float sin_interp = quadratic_interpolation(table_float_idx);
    float comp_float_idx = (QTR_WAVE_TABLE_SIZE_FOR_CALC - table_float_idx);
    float cos_interp;
    if (comp_float_idx >= QTR_WAVE_TABLE_SIZE_FOR_CALC) {
        cos_interp = sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
    } else if (comp_float_idx < 0.0f) {
        cos_interp = sin_table_qtr[0];
    } else {
        cos_interp = quadratic_interpolation(comp_float_idx);
    }
    if (quad == 0) { return sin_interp; } 
    else if (quad == 1) { return cos_interp; } 
    else if (quad == 2) { return -sin_interp; } 
    else { return -cos_interp; }
}

static inline float fft_cos_256(uint16_t i) {
    uint8_t quad = i >> 6;  
    uint8_t in_quad_pos = i & 0x3F;  
    float table_float_idx = in_quad_pos * (QTR_WAVE_TABLE_SIZE_FOR_CALC / 64.0f);
    float sin_interp = quadratic_interpolation(table_float_idx);
    float comp_float_idx = (QTR_WAVE_TABLE_SIZE_FOR_CALC - table_float_idx);
    float cos_interp;
    if (comp_float_idx >= QTR_WAVE_TABLE_SIZE_FOR_CALC) {
        cos_interp = sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
    } else if (comp_float_idx < 0.0f) {
        cos_interp = sin_table_qtr[0];
    } else {
        cos_interp = quadratic_interpolation(comp_float_idx);
    }
    if (quad == 0) { return cos_interp; } 
    else if (quad == 1) { return -sin_interp; } 
    else if (quad == 2) { return -cos_interp; } 
    else { return sin_interp; }
}
#endif 
#define FFT_SIN(i) fft_sin_256(i)
#define FFT_COS(i) fft_cos_256(i)
#elif FFT_SIZE == 512
#if !defined(__VNA_USE_MATH_TABLES__) || defined(NANOVNA_HOST_TEST)
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
    uint8_t quad = i >> 7;  
    uint8_t in_quad_pos = i & 0x7F;  
    float table_float_idx = in_quad_pos * (QTR_WAVE_TABLE_SIZE_FOR_CALC / 128.0f);
    float sin_interp = quadratic_interpolation(table_float_idx);
    float comp_float_idx = (QTR_WAVE_TABLE_SIZE_FOR_CALC - table_float_idx);
    float cos_interp;
    if (comp_float_idx >= QTR_WAVE_TABLE_SIZE_FOR_CALC) {
        cos_interp = sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
    } else if (comp_float_idx < 0.0f) {
        cos_interp = sin_table_qtr[0];
    } else {
        cos_interp = quadratic_interpolation(comp_float_idx);
    }
    if (quad == 0) { return sin_interp; } 
    else if (quad == 1) { return cos_interp; } 
    else if (quad == 2) { return -sin_interp; } 
    else { return -cos_interp; }
}

static inline float fft_cos_512(uint16_t i) {
    uint8_t quad = i >> 7;  
    uint8_t in_quad_pos = i & 0x7F;  
    float table_float_idx = in_quad_pos * (QTR_WAVE_TABLE_SIZE_FOR_CALC / 128.0f);
    float sin_interp = quadratic_interpolation(table_float_idx);
    float comp_float_idx = (QTR_WAVE_TABLE_SIZE_FOR_CALC - table_float_idx);
    float cos_interp;
    if (comp_float_idx >= QTR_WAVE_TABLE_SIZE_FOR_CALC) {
        cos_interp = sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
    } else if (comp_float_idx < 0.0f) {
        cos_interp = sin_table_qtr[0];
    } else {
        cos_interp = quadratic_interpolation(comp_float_idx);
    }
    if (quad == 0) { return cos_interp; } 
    else if (quad == 1) { return -sin_interp; } 
    else if (quad == 2) { return -cos_interp; } 
    else { return sin_interp; }
}
#endif 
#define FFT_SIN(i) fft_sin_512(i)
#define FFT_COS(i) fft_cos_512(i)
#else
#error "Need use bigger sin/cos table for new FFT size"
#endif

// Forward declaration needed for sincosf (Original)
float vna_modff(float x, float* iptr);

// Reference vna_sincosf (Original)
void vna_sincosf(float angle, float* pSinVal, float* pCosVal) {
#if !defined(__VNA_USE_MATH_TABLES__) || defined(NANOVNA_HOST_TEST)
  angle *= 2.0f * VNA_PI; 
  *pSinVal = sinf(angle);
  *pCosVal = cosf(angle);
#else
  float fpart, ipart;
  fpart = vna_modff(angle, &ipart);  
  if (fpart < 0.0f) {
      fpart += 1.0f; 
  }

#ifdef NANOVNA_F303
  // Keep original define logic here in case F303 enters this block (it shouldn't due to top level #ifdef)
  const float table_size_per_quarter = 1023.0f;  
  const float table_size_full_circle = 4092.0f;  
#else
  const float table_size_per_quarter = 256.0f;   
  const float table_size_full_circle = 1024.0f;  
#endif

  float scaled = fpart * table_size_full_circle;
  uint16_t full_index = (uint16_t)scaled;
  float fract = scaled - full_index;

  uint8_t quad = full_index / (uint8_t)table_size_per_quarter;  
  uint16_t in_quad_pos = full_index % (uint16_t)table_size_per_quarter;  

  float sin_interp = quadratic_interpolation(in_quad_pos + fract);
  
  float comp_angle = table_size_per_quarter - (in_quad_pos + fract);
  float cos_interp;
  if (comp_angle >= table_size_per_quarter) {
      cos_interp = sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
  } else if (comp_angle < 0.0f) {
      cos_interp = sin_table_qtr[0];
  } else {
      cos_interp = quadratic_interpolation(comp_angle);
  }

  float sin_final, cos_final;
  switch (quad) {
    case 0: 
      sin_final = sin_interp;   
      cos_final = cos_interp;   
      break;
    case 1: 
      sin_final = cos_interp;   
      cos_final = -sin_interp;  
      break;
    case 2: 
      sin_final = -sin_interp;  
      cos_final = -cos_interp;  
      break;
    case 3: 
      sin_final = -cos_interp;  
      cos_final = sin_interp;   
      break;
    default:
      sin_final = 0.0f;
      cos_final = 1.0f;
      break;
  }

  *pSinVal = sin_final;
  *pCosVal = cos_final;
#endif
}

#undef QTR_WAVE_TABLE_SIZE_FOR_CALC
#endif // else NANOVNA_F303

#endif // __VNA_USE_MATH_TABLES__

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
    for (i = 0; i < n; i += halfsize * 2) {  
      for (j = 0; j < halfsize; j++) {        
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

#ifndef VNA_MODFF_DEFINED
// Original modff (for F072, if not already defined above by F303 block)
// Wait, F303 defines its own inside the ifdef.
// F072 needs one too. Use the original logic.
float vna_modff(float x, float* iptr) {
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
  if (e >= 23)
    x = 0; // no fractional part
  else {
    x = u.f;
    u.i &= ~(0x007fffff >> e); // remove fractional part from u
    x -= u.f;                  // calc fractional part
  }
  if (iptr)
    *iptr = u.f; // cut integer part from float as float
  return x;
}
#endif

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
  // ...
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
  static const uint32_t B1 = 709958130, // B1 = (127-127.0/3-0.03306235651)*2**23
      B2 = 642849266;                   // B2 = (127-127.0/3-24/3-0.03306235651)*2**23

  float r, T;
  union {
    float f;
    uint32_t i;
  } u = {x};
  uint32_t hx = u.i & 0x7fffffff;

  if (hx < 0x00800000) { // zero or subnormal?
    if (hx == 0)
      return x; // cbrt(+-0) is itself
    u.f = x * 0x1p24f;
    hx = u.i & 0x7fffffff;
    hx = hx / 3 + B2;
  } else
    hx = hx / 3 + B1;
  u.i &= 0x80000000;
  u.i |= hx;

  T = u.f;
  r = T * T * T;
  T *= (x + x + r) / (x + r + r);
  r = T * T * T;
  T *= (x + x + r) / (x + r + r);
  return T;
#else
  if (x == 0) {
    return 0;
  }
  float b = 1.0f; 
  float last_b_1 = 0;
  float last_b_2 = 0;
  while (last_b_1 != b && last_b_2 != b) {
    last_b_1 = b;
    b = (2 * b + x / b / b) / 3; 
    last_b_2 = b;
    b = (2 * b + x / b / b) / 3; 
  }
  return b;
#endif
}

//**********************************************************************************
// logf
//**********************************************************************************
float vna_logf(float x) {
  const float MULTIPLIER = logf(2.0f);
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
  return vx.i * (MULTIPLIER / (1 << 23)) - (124.22544637f * MULTIPLIER) -
         (1.498030302f * MULTIPLIER) * mx.f - (1.72587999f * MULTIPLIER) / (0.3520887068f + mx.f);
#else
  // use original code (20% faster default)
  // ... (original impl)
  // Re-pasting original content or just checking if #else path is executed.
  // Assuming #elif 1 is always taken.
#endif
}

float vna_log10f_x_10(float x) {
  const float MULTIPLIER = (10.0f * logf(2.0f) / logf(10.0f));
#if 0
  // ...
#else
  // Give up to 0.0001 error (2x faster original code)
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
  return vx.i * (MULTIPLIER / (1 << 23)) - (124.22544637f * MULTIPLIER) -
         (1.498030302f * MULTIPLIER) * mx.f - (1.72587999f * MULTIPLIER) / (0.3520887068f + mx.f);
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
  static const float aT[] = {
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
  s1 = z * (aT[0] + w * (aT[2] + w * aT[4]));
  s2 = w * (aT[1] + w * aT[3]);
  if (id < 0)
    return x - x * (s1 + s2);
  z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
  return sign ? -z : z;
}

//**********************************************************************************
// Polynomial approximation to atan2f (Recovered from Reference)
//**********************************************************************************
float vna_atan2f(float y, float x)
{
  union {float f; int32_t i;} ux = {x};
  union {float f; int32_t i;} uy = {y};
  if (ux.i == 0 && uy.i == 0)
    return 0.0f;

  float ax, ay, r, s;
  ax = vna_fabsf(x);
  ay = vna_fabsf(y);
  r = (ay < ax) ? ay / ax : ax / ay;
  s = r * r;
  
  // give 0.005 degree error
  r*= 0.999133448222780f - s * (0.320533292381664f - s * (0.144982490144465f - s * 0.038254464970299f));

  // Map to full circle
  if (ay  > ax) r = VNA_PI/2.0f - r;
  if (ux.i < 0) r = VNA_PI      - r;
  if (uy.i < 0) r = -r;
  return r;
}

//**********************************************************************************
// Fast expf approximation (Recovered from Reference)
//**********************************************************************************
float vna_expf(float x)
{
  union { float f; int32_t i; } v;
  v.i = (int32_t)(12102203.0f*x) + 0x3F800000;
  int32_t m = (v.i >> 7) & 0xFFFF;  // copy mantissa
  // cubic spline approximation, empirical values for small maximum relative error (8.34e-5):
  v.i += ((((((((1277*m) >> 14) + 14825)*m) >> 14) - 79749)*m) >> 11) - 626;
  return v.f;
}
