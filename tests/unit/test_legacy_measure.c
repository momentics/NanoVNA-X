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

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "nanovna.h"

/*
 * Regression suite for the math-only helpers in src/rf/analysis/legacy_measure.c.
 *
 * The legacy RF analytics module performs all of its cursor searches and
 * regressions on host-side float buffers before any UI gets involved.  Because
 * these helpers are self-contained (no hardware dependencies), we can run
 * high-confidence tests on every GitHub build and immediately detect when a
 * refactor breaks the interpolation math, marker bookkeeping, or polynomial
 * fits.  Each test below feeds synthetic sweep data into the exact routines the
 * firmware uses and asserts the resulting frequencies/coefficients.
 */

uint16_t sweep_points;
freq_t frequency0;
freq_t frequency1;
float measure_frequency_step;
alignas(8) float measured[2][SWEEP_POINTS_MAX][2];
config_t config;
properties_t current_props;

static float g_curve_data[SWEEP_POINTS_MAX];
static float g_regression_x[32];
static float g_regression_y[32];
static int g_failures = 0;
static int g_last_marker_slot = -1;
static int g_last_marker_index = -1;

#define CHECK(cond)                                                                           \
  do {                                                                                        \
    if (!(cond)) {                                                                            \
      ++g_failures;                                                                           \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
    }                                                                                         \
  } while (0)

static void expect_float_close(float expected, float actual, float tol, const char* label) {
  if (fabsf(expected - actual) > tol) {
    ++g_failures;
    fprintf(stderr, "[FAIL] %s expected=%f actual=%f tol=%f\n", label, expected, actual, tol);
  }
}

static void configure_sweep(uint16_t points, freq_t start, float step_hz) {
  sweep_points = points;
  frequency0 = start;
  measure_frequency_step = step_hz;
  frequency1 = start + (freq_t)((points > 0 ? (points - 1) : 0) * step_hz);
}

static void reset_marker_log(void) {
  g_last_marker_slot = -1;
  g_last_marker_index = -1;
}

void set_marker_index(int marker, int idx) {
  g_last_marker_slot = marker;
  g_last_marker_index = idx;
}

freq_t get_frequency(uint16_t idx) {
  return frequency0 + (freq_t)(measure_frequency_step * idx);
}

freq_t get_frequency_step(void) {
  return (freq_t)measure_frequency_step;
}

freq_t get_sweep_frequency(uint16_t idx) {
  return get_frequency(idx);
}

static float curve_value(uint16_t idx) {
  return g_curve_data[idx];
}

static float regression_get_x(uint16_t idx) {
  return g_regression_x[idx];
}

static float regression_get_y(uint16_t idx) {
  return g_regression_y[idx];
}



// Mocks for legacy_measure dependencies
float resistance(int i, const float* v) { (void)i; (void)v; return 50.0f; }
float reactance(int i, const float* v) { (void)i; (void)v; return 0.0f; }
float swr(int i, const float* v) { (void)i; (void)v; return 1.0f; }
float logmag(int i, const float* v) { (void)i; (void)v; return 0.0f; }
void invalidate_rect(int x, int y, int w, int h) {
  (void)x; (void)y; (void)w; (void)h;
}
void cell_printf(int x, int y, const char* fmt, ...) {
  (void)x; (void)y; (void)fmt;
}
void markmap_all_markers(void) {}

#include "rf/analysis/measurement_analysis.h"

static void test_match_quadratic_equation(void) {
  /*
   * The quadratic solver underpins both marker searches and the LC matching
   * math.  Two scenarios are covered: a classic polynomial with real roots and
   * another with a negative discriminant that should clamp to zeros.
   */
  float roots[2];
  match_quadratic_equation(1.0f, -5.0f, 6.0f, roots); /* (x-2)(x-3)=0 */
  expect_float_close(3.0f, roots[0], 1e-6f, "quadratic root[0]");
  expect_float_close(2.0f, roots[1], 1e-6f, "quadratic root[1]");

  match_quadratic_equation(1.0f, 0.0f, 4.0f, roots); /* no real roots */
  expect_float_close(0.0f, roots[0], 1e-6f, "quadratic no-root[0]");
  expect_float_close(0.0f, roots[1], 1e-6f, "quadratic no-root[1]");
}

static void build_symmetric_parabola(float center) {
  for (uint16_t i = 0; i < sweep_points; ++i) {
    float delta = (float)i - center;
    g_curve_data[i] = delta * delta;
  }
}

static void test_measure_search_value_right(void) {
  /*
   * Searching from the left of a crossing should produce the expected marker
   * book-keeping, and the returned frequency must be interpolated between
   * samples.  The parabola y=(x-5)^2 crosses y=4 at x=3,7; here we start left
   * of the first crossing and scan right.
   */
  configure_sweep(16, 1000000U, 1000.0f);
  build_symmetric_parabola(5.0f);
  reset_marker_log();

  uint16_t idx = 2;
  float freq = measure_search_value(&idx, 4.0f, curve_value, MEASURE_SEARCH_RIGHT, 5);
  const float expected = (float)frequency0 + measure_frequency_step * 3.0f;
  expect_float_close(expected, freq, 0.5f, "search right frequency");
  CHECK(idx == 2); /* marker sticks to the last >y sample */
  CHECK(g_last_marker_slot == 5);
  CHECK(g_last_marker_index == 2);
}

static void test_measure_search_value_left(void) {
  /*
   * Mirrored search starting to the right of the same crossing.  The helper
   * should step backwards, update the caller's index to the last <= y element,
   * and emit a negative fractional offset so the frequency remains correct.
   */
  configure_sweep(16, 2000000U, 500.0f);
  build_symmetric_parabola(5.0f);
  reset_marker_log();

  uint16_t idx = 8;
  float freq = measure_search_value(&idx, 4.0f, curve_value, MEASURE_SEARCH_LEFT, 6);
  const float expected = (float)frequency0 + measure_frequency_step * 7.0f;
  expect_float_close(expected, freq, 0.5f, "search left frequency");
  CHECK(idx == 8);
  CHECK(g_last_marker_slot == 6);
  CHECK(g_last_marker_index == 8);
}

static void test_search_peak_value_max(void) {
  /*
   * search_peak_value() scans the sweep for a global extremum and refines it
   * with a parabolic interpolation.  A downward-opening parabola guarantees the
   * function locates a clean maximum away from the edges.
   */
  configure_sweep(9, 0U, 1.0f);
  for (uint16_t i = 0; i < sweep_points; ++i) {
    float delta = (float)i - 4.0f;
    g_curve_data[i] = 10.0f - delta * delta;
  }
  uint16_t peak_idx = 0;
  float peak = search_peak_value(&peak_idx, curve_value, MEASURE_SEARCH_MAX);
  expect_float_close(10.0f, peak, 1e-5f, "search peak max value");
  CHECK(peak_idx == 4);
}

static void test_search_peak_value_min(void) {
  /*
   * The same routine with MEASURE_SEARCH_MIN must return the minimum of an
   * upward-opening parabola and provide the correct cursor index.
   */
  configure_sweep(11, 0U, 1.0f);
  for (uint16_t i = 0; i < sweep_points; ++i) {
    float delta = (float)i - 5.0f;
    g_curve_data[i] = delta * delta;
  }
  uint16_t min_idx = 0;
  float trough = search_peak_value(&min_idx, curve_value, MEASURE_SEARCH_MIN);
  expect_float_close(0.0f, trough, 1e-6f, "search peak min value");
  CHECK(min_idx == 5);
}

static void test_parabolic_regression(void) {
  /*
   * parabolic_regression() solves a 3x3 normal equation for a polynomial fit.
   * Feed it synthetic y = 1 + 2x + 0.5x^2 samples and verify that the fitted
   * coefficients match the ground truth within a tiny epsilon.
   */
  const int samples = 6;
  for (int i = 0; i < samples; ++i) {
    float x = (float)i;
    g_regression_x[i] = x;
    g_regression_y[i] = 1.0f + 2.0f * x + 0.5f * x * x;
  }
  float coeff[3] = {0};
  parabolic_regression(samples, regression_get_x, regression_get_y, coeff);
  expect_float_close(1.0f, coeff[0], 1e-5f, "regression coeff a");
  expect_float_close(2.0f, coeff[1], 1e-5f, "regression coeff b");
  expect_float_close(0.5f, coeff[2], 1e-5f, "regression coeff c");
}

int main(void) {
  memset(&config, 0, sizeof(config));
  config._measure_r = 50.0f;
  test_match_quadratic_equation();
  test_measure_search_value_right();
  test_measure_search_value_left();
  test_search_peak_value_max();
  test_search_peak_value_min();
  test_parabolic_regression();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_legacy_measure");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
