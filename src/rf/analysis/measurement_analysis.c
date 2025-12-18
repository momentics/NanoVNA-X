/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on legacy_measure.c by Dmitry (DiSlord)
 * All rights reserved.
 */

#include "rf/analysis/measurement_analysis.h"
#include <stdalign.h>
#include <math.h>
#include "ui/display/traces.h"

extern alignas(8) float measured[2][SWEEP_POINTS_MAX][2];

#ifndef frequency0
extern freq_t frequency0;
#endif

#ifndef frequency1
extern freq_t frequency1;
#endif

#ifndef sweep_points
extern uint16_t sweep_points;
#endif

// Memory for measure cache data (Definition)
char alignas(8) measure_memory[128];

// Internal pointers for easy access within analysis functions
static lc_match_array_t* lc_match_array = (lc_match_array_t*)measure_memory;
static s21_analysis_t* s21_measure = (s21_analysis_t*)measure_memory;
static s11_resonance_measure_t* s11_resonance = (s11_resonance_measure_t*)measure_memory;

// ================================================================================================
// Math Helpers
// ================================================================================================

void match_quadratic_equation(float a, float b, float c, float* x) {
  const float a_x_2 = 2.0f * a;
  if (fabsf(a_x_2) < VNA_EPSILON) {
     if (fabsf(b) > VNA_EPSILON) {
         x[0] = x[1] = -c / b;
     } else {
         x[0] = x[1] = 0.0f;
     }
     return;
  }
  const float d = (b * b) - (2.0f * a_x_2 * c);
  if (d < 0) {
    x[0] = x[1] = 0.0f;
    return;
  }
  const float sd = vna_sqrtf(d);
  x[0] = (-b + sd) / a_x_2;
  x[1] = (-b - sd) / a_x_2;
}

float bilinear_interpolation(float y1, float y2, float y3, float x) {
  const float a = 0.5f * (y1 + y3) - y2;
  const float b = 0.5f * (y3 - y1);
  const float c = y2;
  return a * x * x + b * x + c;
}

// ================================================================================================
// Search Logic
// ================================================================================================

float measure_search_value(uint16_t* idx, float y, get_value_t get, int16_t mode,
                                  int16_t marker_idx) {
  uint16_t x = *idx;
  float y1, y2, y3;
  y1 = y2 = y3 = get(x);
  bool result = (y3 > y); // current position depend from start point
  for (; x < sweep_points; x += mode) {
    if (mode < 0 && x == 0) break;
    y3 = get(x);
    if (result != (y3 > y))
      break;
    y1 = y2;
    y2 = y3;
  }
  if (x >= sweep_points)
    return 0;
  x -= mode;
  *idx = x;
  if (marker_idx != MARKER_INVALID)
    set_marker_index(marker_idx, x);
  // Now y1 > y, y2 > y, y3 <= y or y1 < y, y2 < y, y3 >= y
  const float a = 0.5f * (y1 + y3) - y2;
  const float b = 0.5f * (y3 - y1);
  const float c = y2 - y;
  float r[2];
  match_quadratic_equation(a, b, c, r);
  // Select result in middle 0 and 1 (in middle y2 and y3 result)
  float res = (r[0] > 0 && r[0] < 1.0) ? r[0] : r[1];
  // for search left need swap y1 and y3 points (use negative result)
  if (mode < 0)
    res = -res;
  return get_frequency(x) + get_frequency_step() * res;
}

static bool _greaterf(float x, float y) { return x > y; }
static bool _lesserf(float x, float y) { return x < y; }

float search_peak_value(uint16_t* xp, get_value_t get, bool mode) {
  bool (*compare)(float x, float y) = mode ? _greaterf : _lesserf;
  uint16_t x = 0;
  float y2 = get(x), ytemp;
  for (int i = 1; i < sweep_points; i++) {
    if (compare(ytemp = get(i), y2)) {
      y2 = ytemp;
      x = i;
    }
  }
  if (x < 1 || x >= sweep_points - 1)
    return y2;
  *xp = x;
  float y1 = get(x - 1);
  float y3 = get(x + 1);
  if (y1 == y3)
    return y2;

  const float a = 8.0f * (y1 - 2.0f * y2 + y3);
  const float b = y3 - y1;
  const float c = y2;
  if (fabsf(a) < VNA_EPSILON) return c;
  return c - b * b / a;
}

bool measure_get_value(uint16_t ch, freq_t f, float* data) {
  if (f < frequency0 || f > frequency1)
    return false;
  // Search k1
  uint16_t _points = sweep_points - 1;
  freq_t span = frequency1 - frequency0;
  uint32_t idx = (uint64_t)(f - frequency0) * (uint64_t)_points / span;
  if (idx < 1 || idx >= _points)
    return false;
  uint64_t v = (uint64_t)span * idx + _points / 2;
  freq_t src_f0 = frequency0 + (v) / _points;
  freq_t src_f1 = frequency0 + (v + span) / _points;
  freq_t delta = src_f1 - src_f0;
  float k1 = (delta == 0) ? 0.0f : (float)(f - src_f0) / delta;
#if 1
  // Bilinear interpolation by k1
  data[0] = bilinear_interpolation(measured[ch][idx - 1][0], measured[ch][idx][0],
                                   measured[ch][idx + 1][0], k1);
  data[1] = bilinear_interpolation(measured[ch][idx - 1][1], measured[ch][idx][1],
                                   measured[ch][idx + 1][1], k1);
#else
  // Linear Interpolate by k1
  float k0 = 1.0 - k1;
  data[0] = measured[ch][idx][0] * k0 + measured[ch][idx + 1][0] * k1;
  data[1] = measured[ch][idx][1] * k0 + measured[ch][idx + 1][1] * k1;
#endif
  return true;
}

// ================================================================================================
// Regression Logic
// ================================================================================================

void parabolic_regression(int N, get_value_t getx, get_value_t gety, float* result) {
  float x, y, xx, xy, xxy, xxx, xxxx, _x, _y, _xx, _xy;
  x = y = xx = xy = xxy = xxx = xxxx = 0.0f;
  for (int i = 0; i < N; ++i) {
    _x = getx(i);
    _y = gety(i); // Get x and y
    _xx = _x * _x;
    _xy = _x * _y;
    x += _x;
    y += _y; // SUMM(x^1) and SUMM(x^0 * y)
    xx += _xx;
    xy += _xy; // SUMM(x^2) and SUMM(x^1 * y)
    xxx += _x * _xx;
    xxy += _x * _xy;   // SUMM(x^3) and SUMM(x^2 * y)
    xxxx += _xx * _xx; // SUMM(x^4)
  }
  float xm = x / N, ym = y / N, xxm = xx / N, a, b, c;
  xxxx -= xx * xxm;
  xxx -= xx * xm;
  xxy -= xx * ym;
  xx -= x * xm;
  xy -= x * ym;
  c = (xx * xxy - xxx * xy) / (xxxx * xx - xxx * xxx);
  b = (xxxx * xy - xxx * xxy) / (xxxx * xx - xxx * xxx);
  a = ym - b * xm - c * xxm;
  result[0] = a;
  result[1] = b;
  result[2] = c;
}

void linear_regression(int N, get_value_t getx, get_value_t gety, float* result) {
  float x, y, xx, xy, _x, _y, _xx, _xy;
  x = y = xx = xy = 0.0f;
  for (int i = 0; i < N; ++i) {
    _x = getx(i);
    _y = gety(i); // Get x and y
    _xx = _x * _x;
    _xy = _x * _y;
    x += _x;
    y += _y; // SUMM(x^1) and SUMM(x^0 * y)
    xx += _xx;
    xy += _xy; // SUMM(x^2) and SUMM(x^1 * y)
  }
  float xm = x / N, ym = y / N, a, b;
  b = (xy - x * ym) / (xx - x * xm);
  a = ym - b * xm;
  result[0] = a;
  result[1] = b;
}

// ================================================================================================
// LC Match Logic
// ================================================================================================

static void lc_match_calc_hi(float R0, float RL, float XL, t_lc_match* matches) {
  float xp[2];
  const float a = R0 - RL;
  const float b = 2.0f * XL * R0;
  const float c = R0 * (XL * XL + RL * RL);
  match_quadratic_equation(a, b, c, xp);
  const float XL1 = XL + xp[0];
  matches[0].xs = xp[0] * xp[0] * XL1 / (RL * RL + XL1 * XL1) - xp[0];
  matches[0].xps = 0.0f;
  matches[0].xpl = xp[0];
  const float XL2 = XL + xp[1];
  matches[1].xs = xp[1] * xp[1] * XL2 / (RL * RL + XL2 * XL2) - xp[1];
  matches[1].xps = 0.0f;
  matches[1].xpl = xp[1];
}

static void lc_match_calc_lo(float R0, float RL, float XL, t_lc_match* matches) {
  float xs[2];
  const float a = 1.0f;
  const float b = 2.0f * XL;
  const float c = RL * RL + XL * XL - R0 * RL;
  match_quadratic_equation(a, b, c, xs);
  const float XL1 = XL + xs[0];
  const float RL1 = RL - R0;
  matches[0].xs = xs[0];
  matches[0].xps = -R0 * R0 * XL1 / (RL1 * RL1 + XL1 * XL1);
  matches[0].xpl = 0.0f;
  const float XL2 = XL + xs[1];
  matches[1].xs = xs[1];
  matches[1].xps = -R0 * R0 * XL2 / (RL1 * RL1 + XL2 * XL2);
  matches[1].xpl = 0.0f;
}

int16_t lc_match_calc(int index) {
  const float R0 = lc_match_array->R0;
  const float* coeff = measured[0][index];
  const float RL = resistance(index, coeff);
  const float XL = reactance(index, coeff);

  if (RL <= 0.5f)
    return -1;

  const float q_factor = XL / RL;
  const float vswr = swr(index, coeff);
  if (vswr <= 1.1f || q_factor >= 100.0f)
    return 0;

  t_lc_match* matches = lc_match_array->matches;
  if ((RL * 1.1f) > R0 && RL < (R0 * 1.1f)) {
    matches[0].xpl = 0.0f;
    matches[0].xps = 0.0f;
    matches[0].xs = -XL;
    return 1;
  }
  int16_t n = 0;
  if (RL >= R0 || RL * RL + XL * XL > R0 * RL) {
    lc_match_calc_hi(R0, RL, XL, &matches[0]);
    if (RL >= R0)
      return 2;
    n = 2;
  }
  lc_match_calc_lo(R0, RL, XL, &matches[n]);
  return n + 2;
}

// ================================================================================================
// S21 Logic
// ================================================================================================

float s21pow2(uint16_t i) {
  const float re = measured[1][i][0];
  const float im = measured[1][i][1];
  return re * re + im * im;
}

float s21tan(uint16_t i) {
  const float re = measured[1][i][0];
  const float im = measured[1][i][1];
  return im / re;
}

float s21logmag(uint16_t i) {
  return logmag(i, measured[1][i]);
}

void analysis_lcshunt(void) {
  uint16_t xp = 0, x2;
  s21_measure->header = "LC-SHUNT";
  float ypeak = search_peak_value(&xp, s21pow2, MEASURE_SEARCH_MIN);
  float att = vna_sqrtf(ypeak);
  s21_measure->r = config._measure_r * att / (2.0f * (1.0f - att));
  if (s21_measure->r < 0.0f)
    return;
  set_marker_index(0, xp);

  float tan45 = config._measure_r / (config._measure_r + 4.0f * s21_measure->r);
  x2 = xp;
  float f1 = measure_search_value(&x2, -tan45, s21tan, MEASURE_SEARCH_LEFT, 1);
  if (f1 == 0)
    return;

  x2 = xp;
  float f2 = measure_search_value(&x2, tan45, s21tan, MEASURE_SEARCH_RIGHT, 2);
  if (f2 == 0)
    return;

  float bw = f2 - f1;
  float fpeak = vna_sqrtf(f2 * f1);
  s21_measure->freq = fpeak;
  s21_measure->q = fpeak / bw;
  s21_measure->l = s21_measure->r / ((2.0f * VNA_PI) * bw);
  s21_measure->c = bw / ((2.0f * VNA_PI) * fpeak * fpeak * s21_measure->r);
}

void analysis_lcseries(void) {
  uint16_t xp = 0, x2;
  s21_measure->header = "LC-SERIES";
  float ypeak = search_peak_value(&xp, s21pow2, MEASURE_SEARCH_MAX);
  if (xp == 0)
    return;
  s21_measure->r = 2 * config._measure_r * (1.0f / vna_sqrtf(ypeak) - 1.0f);
  if (s21_measure->r < 0)
    return;
  set_marker_index(0, xp);

  const float tan45 = 1.0f;
  x2 = xp;
  float f1 = measure_search_value(&x2, tan45, s21tan, MEASURE_SEARCH_LEFT, 1);
  if (f1 == 0)
    return;

  x2 = xp;
  float f2 = measure_search_value(&x2, -tan45, s21tan, MEASURE_SEARCH_RIGHT, 2);
  if (f2 == 0)
    return;

  float bw = f2 - f1;
  float fpeak = vna_sqrtf(f2 * f1);
  float reff = 2.0f * config._measure_r + s21_measure->r;

  s21_measure->freq = fpeak;
  s21_measure->l = reff / ((2.0f * VNA_PI) * bw);
  s21_measure->c = bw / ((2.0f * VNA_PI) * fpeak * fpeak * reff);
  s21_measure->q = (2.0f * VNA_PI) * fpeak * s21_measure->l / s21_measure->r;
}

void analysis_xtalseries(void) {
  analysis_lcseries();
  s21_measure->header = "XTAL-SERIES";
  uint16_t xp = 0;
  search_peak_value(&xp, s21pow2, MEASURE_SEARCH_MIN);
  if (xp == 0)
    return;
  set_marker_index(3, xp);

  freq_t freq1 = get_frequency(xp);
  if (freq1 < s21_measure->freq)
    return;
  s21_measure->freq1 = freq1;
  s21_measure->df = s21_measure->freq1 - s21_measure->freq;
  s21_measure->c1 = s21_measure->c * s21_measure->freq / (2.0f * s21_measure->df);
}

// ================================================================================================
// Filter Pass Logic
// ================================================================================================

static const float filter_att[_end] = {3.0f, 6.0f, 10.0f, 20.0f /*, 60.0f*/};

void find_filter_pass(float max, s21_pass* p, uint16_t idx, int16_t mode) {
  for (int i = 0; i < _end; i++)
    p->f[i] = measure_search_value(&idx, max - filter_att[i], s21logmag, mode,
                                   i == 0 ? (mode == MEASURE_SEARCH_LEFT ? 1 : 2) : MARKER_INVALID);
  p->decade = p->octave = 0.0f;
  if (p->f[_10dB] != 0 && p->f[_20dB] != 0) {
    float k = vna_fabsf(vna_logf(p->f[_20dB]) - vna_logf(p->f[_10dB]));
    p->decade = (10.0f * logf(10.0f)) / k;
    p->octave = (10.0f * logf(2.0f)) / k;
  }
}

// ================================================================================================
// S11 Logic
// ================================================================================================

float s11imag(uint16_t i) {
  return measured[0][i][1];
}

float s11loss(uint16_t i) {
  return -0.5f * logmag(i, measured[0][i]);
}

float s11index(uint16_t i) {
  return vna_sqrtf(get_frequency(i) * 1e-9f);
}

// Resonance Logic

float s11_resonance_value(uint16_t i) {
  return measured[0][i][1];
}

float s11_resonance_min(uint16_t i) {
  return fabsf(reactance(i, measured[0][i]));
}

bool add_resonance_value(int i, uint16_t x, freq_t f) {
  float data[2];
  if (measure_get_value(0, f, data)) {
    s11_resonance->data[i].f = f;
    s11_resonance->data[i].r = resistance(x, data);
    s11_resonance->data[i].x = reactance(x, data);
    return true;
  }
  return false;
}
