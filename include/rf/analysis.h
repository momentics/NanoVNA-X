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

#ifndef __MEASUREMENT_ANALYSIS_H__
#define __MEASUREMENT_ANALYSIS_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include "nanovna.h" // For freq_t, etc.

// Shared memory for analysis results
// Shared memory for analysis results
// extern char alignas(8) measure_memory[128]; // Replaced by measurement_cache_t

// Type of get value function
typedef float (*get_value_t)(uint16_t idx);

// Search constants
#define MEASURE_SEARCH_LEFT -1
#define MEASURE_SEARCH_RIGHT 1
#define MEASURE_SEARCH_MIN 0
#define MEASURE_SEARCH_MAX 1

// Math / Search functions
void match_quadratic_equation(float a, float b, float c, float* x);
float measure_search_value(uint16_t* idx, float y, get_value_t get, int16_t mode, int16_t marker_idx);
float search_peak_value(uint16_t* xp, get_value_t get, bool mode);
float bilinear_interpolation(float y1, float y2, float y3, float x);
bool measure_get_value(uint16_t ch, freq_t f, float* data); // Requires global access
void parabolic_regression(int N, get_value_t getx, get_value_t gety, float* result);
void linear_regression(int N, get_value_t getx, get_value_t gety, float* result);

// LC Matching Structures
typedef struct {
  float xps; // Reactance parallel to source
  float xs;  // Serial reactance
  float xpl; // Reactance parallel to load
} t_lc_match;

typedef struct {
  freq_t Hz;
  float R0;
  t_lc_match matches[4];
  int16_t num_matches;
} lc_match_array_t;

int16_t lc_match_calc(int index);

// S21 Analysis Structures
typedef struct {
  const char* header;
  freq_t freq;  // resonant frequency
  freq_t freq1; // fp
  uint32_t df;  // delta f
  float l;
  float c;
  float c1; // capacitor parallel
  float r;
  float q; // Q factor
} s21_analysis_t;

// S21 Helper functions
float s21pow2(uint16_t i);
float s21tan(uint16_t i);
float s21logmag(uint16_t i);

void analysis_lcshunt(void);
void analysis_lcseries(void);
void analysis_xtalseries(void);

// Filter Analysis Structures
enum { _3dB = 0, _6dB, _10dB, _20dB /*, _60dB*/, _end };
// extern const float filter_att[_end]; // Needed? Probably internal to implementation

typedef struct {
  float f[_end];
  float decade;
  float octave;
} s21_pass;

typedef struct {
  float fmax;
  float vmax;
  s21_pass lo_pass;
  s21_pass hi_pass;
  float f_center;
  float bw_3dB;
  float bw_6dB;
  float q;
} s21_filter_measure_t;

void find_filter_pass(float max, s21_pass* p, uint16_t idx, int16_t mode);

// S11 Cable Measure
typedef struct {
  float freq;
  float R;
  float len;
  float loss;
  float mloss;
  float vf;
  float C0;
  float a, b, c;
} s11_cable_measure_t;

float s11imag(uint16_t i);
float s11loss(uint16_t i);
float s11index(uint16_t i);

// S11 Resonance Measure
#define MEASURE_RESONANCE_COUNT 6
typedef struct {
  struct {
    freq_t f;
    float r;
    float x;
  } data[MEASURE_RESONANCE_COUNT];
  uint8_t count;
} s11_resonance_measure_t;

float s11_resonance_value(uint16_t i);
float s11_resonance_min(uint16_t i);
bool add_resonance_value(int i, uint16_t x, freq_t f); // Helper used by update_mask logic, maybe internal?

// Measurement Cache Union
typedef union {
    lc_match_array_t lc_match;
    s21_analysis_t s21;
    s21_filter_measure_t s21_filter;
    s11_cable_measure_t s11_cable;
    s11_resonance_measure_t s11_resonance;
    char raw[128]; // Ensure minimum size and strict alignment.
} measurement_cache_t;

// Shared memory for analysis results (Typed)
extern alignas(8) measurement_cache_t measure_cache;

// Legacy compatibility macro (temporary, until all files are updated)
// #define measure_memory ((char*)&measure_cache)

// Global Helpers (Wrappers for global vars if needed, but we assume direct access for now to match legacy)

#endif // __MEASUREMENT_ANALYSIS_H__
