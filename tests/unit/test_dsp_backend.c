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

/*
 * Unit tests for the scalar dsp_process() path in src/processing/dsp_backend.c.
 * When the firmware is built on a host (without USE_DSP) this path performs
 * plain C accumulation of quadrature samples; accurate results are critical for
 * the SNR of the measurement pipeline.  By driving it with synthetic capture
 * buffers we ensure regressions are caught in CI.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nanovna.h"

void dsp_process(audio_sample_t *capture, size_t length);
void fetch_amplitude(float *gamma);
void fetch_amplitude_ref(float *gamma);
extern void reset_dsp_accumerator(void);

static int g_failures = 0;

static void expect_close(float expected, float actual, float tol, const char *label) {
  if (fabsf(expected - actual) > tol) {
    ++g_failures;
    fprintf(stderr, "[FAIL] %s expected=%f actual=%f\n", label, expected, actual);
  }
}

static void snapshot(float *samp_s, float *samp_c, float *ref_s, float *ref_c) {
  if (samp_s != NULL || samp_c != NULL) {
    float gamma[2];
    fetch_amplitude(gamma);
    if (samp_s != NULL)
      *samp_s = gamma[0] * 1e9f;
    if (samp_c != NULL)
      *samp_c = gamma[1] * 1e9f;
  }
  if (ref_s != NULL || ref_c != NULL) {
    float gamma[2];
    fetch_amplitude_ref(gamma);
    if (ref_s != NULL)
      *ref_s = gamma[0] * 1e9f;
    if (ref_c != NULL)
      *ref_c = gamma[1] * 1e9f;
  }
}

static void test_dc_signal(void) {
  reset_dsp_accumerator();
  float capture[2 * AUDIO_SAMPLES_COUNT];
  for (size_t i = 0; i < AUDIO_SAMPLES_COUNT; ++i) {
    capture[i * 2 + 0] = 100.0f; /* reference */
    capture[i * 2 + 1] = 50.0f;  /* sample */
  }
  dsp_process(capture, sizeof(capture) / sizeof(capture[0]));

  float s_s = 0.0f, s_c = 0.0f, r_s = 0.0f, r_c = 0.0f;
  snapshot(&s_s, &s_c, &r_s, &r_c);
  expect_close(0.0f, s_s, 2e3f, "sample*sin should be ~0 for DC");
  expect_close(0.0f, r_s, 2e3f, "ref*sin should be ~0 for DC");
}

static void test_in_phase_sine(void) {
  reset_dsp_accumerator();
  float capture[2 * AUDIO_SAMPLES_COUNT];
  for (size_t i = 0; i < AUDIO_SAMPLES_COUNT; ++i) {
    float phase = (2.0f * VNA_PI * i) / AUDIO_SAMPLES_COUNT;
    float ref = sinf(phase);
    float sample = sinf(phase);
    capture[i * 2 + 0] = ref * 1000.0f;
    capture[i * 2 + 1] = sample * 500.0f;
  }
  dsp_process(capture, sizeof(capture) / sizeof(capture[0]));
  float s_s = 0.0f, s_c = 0.0f, r_s = 0.0f, r_c = 0.0f;
  snapshot(&s_s, &s_c, &r_s, &r_c);
  expect_close(0.0f, s_c, 2e3f, "sample*cos should be near zero for pure sine");
}

static void test_quadrature_sine(void) {
  reset_dsp_accumerator();
  float capture[2 * AUDIO_SAMPLES_COUNT];
  for (size_t i = 0; i < AUDIO_SAMPLES_COUNT; ++i) {
    float phase = (2.0f * VNA_PI * i) / AUDIO_SAMPLES_COUNT;
    capture[i * 2 + 0] = sinf(phase) * 500.0f;
    capture[i * 2 + 1] = cosf(phase) * 500.0f; /* 90° shifted */
  }
  dsp_process(capture, sizeof(capture) / sizeof(capture[0]));
  float s_s = 0.0f, s_c = 0.0f, r_s = 0.0f, r_c = 0.0f;
  snapshot(&s_s, &s_c, &r_s, &r_c);
  expect_close(0.0f, s_s, 2e3f, "quadrature sample sin accumulate ~0");
  expect_close(0.0f, r_c, 2e3f, "quadrature ref cos accumulate ~0");
}

static void test_calculate_gamma_sign(void) {
  // Test that calculate_gamma produces correct sign for imaginary part.

  // Define helper prototype
  extern void set_dsp_accumulator(float ss, float sc, float rs, float rc);
  extern void calculate_gamma(float *gamma);
  float gamma[2];

  // Case 1: S=1 (Real), R=1 (Real). Gamma=1.
  // Real S (sc) = 1e9, Imag S (ss) = 0.
  // Real R (rc) = 1e9, Imag R (rs) = 0.
  set_dsp_accumulator(0.0f, 1e9f, 0.0f, 1e9f);
  calculate_gamma(gamma);
  expect_close(1.0f, gamma[0], 0.001f, "Real part S=1,R=1");
  expect_close(0.0f, gamma[1], 0.001f, "Imag part S=1,R=1");

  // Case 2: S=j (Imag), R=1 (Real). Gamma=j.
  // Real S (sc) = 0, Imag S (ss) = 1e9.
  // Real R (rc) = 1e9, Imag R (rs) = 0.
  // Expected: Real=0, Imag=1.
  set_dsp_accumulator(1e9f, 0.0f, 0.0f, 1e9f);
  calculate_gamma(gamma);
  expect_close(0.0f, gamma[0], 0.001f, "Real part S=j,R=1");

  // This is where we expect failure if the bug exists.
  // Current code: Imag = (sc*rs - ss*rc)/rr = (0*0 - 1*1)/1 = -1.
  // Correct code: Imag = (ss*rc - sc*rs)/rr = (1*1 - 0*0)/1 = 1.
  if (gamma[1] < 0.5f) {
    fprintf(stderr, "[FAIL] Imag part expected 1.0, got %f. Confirmed Sign Inversion Bug.\n",
            gamma[1]);
    g_failures++;
  } else {
    // If it passes here, then maybe my understanding of ss/sc mapping is wrong,
    // but let's see the result.
    expect_close(1.0f, gamma[1], 0.001f, "Imag part S=j,R=1");
  }
}

int main(void) {
  test_dc_signal();
  test_in_phase_sine();
  test_quadrature_sine();
  test_calculate_gamma_sign();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_dsp_backend");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
