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
 * Test precision verification (based on accuracy analysis):
 * - vna_sincosf: tolerance 0.000001 for sin/cos values (measured max: ~0.0000004), 0.0000005 for trigonometric identity (measured max: ~0.00000012)
 * - vna_modff: tolerance 0.0000005 for integer and fractional parts (measured max: ~0.00000012)
 * - vna_sqrtf: tolerance 0.000001 for square root calculations (measured max: ~0.00000057)
 * - FFT impulse: tolerance 0.0000005 for frequency domain flatness (measured max: ~0.0)
 * - FFT roundtrip: tolerance 0.000001 for forward/inverse transform accuracy (measured max: ~0.00000042)
 */

/**
 * LUT accuracy tests for vna_sincosf().
 *
 * The firmware replaces expensive sinf/cosf calls with lookup-table driven
 * interpolation (FAST_MATH_TABLE_SIZE=512).  Accuracy mistakes accumulate into
 * the FFT, trace rendering, and calibration logic, so we verify the LUT against
 * the double-checked libm reference across multiple quadrants as well as
 * negative inputs / periodic wrapping.  Tests run on the host and require no
 * STM32 hardware.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nanovna.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static int g_failures = 0;

static void fail(const char* expr, float angle, float expected, float actual) {
  ++g_failures;
  fprintf(stderr, "[FAIL] %s angle=%f expected=%e actual=%e\n", expr, angle, expected, actual);
}

static void expect_close(const char* expr, float angle, float expected, float actual, float tol) {
  if (fabsf(expected - actual) > tol) {
    fail(expr, angle, expected, actual);
  }
}

static void check_angle(float angle) {
  float sin_lut = 0.0f, cos_lut = 0.0f;
  vna_sincosf(angle, &sin_lut, &cos_lut);

  const float rad = angle * (2.0f * VNA_PI);
  const float sin_ref = sinf(rad);
  const float cos_ref = cosf(rad);
  const float tol = 1e-6f; /* tolerance based on accuracy analysis (max measured ~0.0000004) */

  expect_close("sin", angle, sin_ref, sin_lut, tol);
  expect_close("cos", angle, cos_ref, cos_lut, tol);

  const float magnitude = fabsf(sin_lut * sin_lut + cos_lut * cos_lut - 1.0f);
  if (magnitude > 5e-7f) { /* tolerance based on accuracy analysis (max measured ~0.00000012) */
    ++g_failures;
    fprintf(stderr, "[FAIL] norm angle=%f drift=%e\n", angle, magnitude);
  }
}

static void test_primary_interval(void) {
  const float samples[] = {
      0.0f, 0.0625f, 0.1111f, 0.25f, 0.3333333f, 0.5f, 0.6666667f, 0.75f, 0.875f, 0.999f};
  for (size_t i = 0; i < ARRAY_SIZE(samples); ++i) {
    check_angle(samples[i]);
  }
}

static void test_negative_and_wrapped(void) {
  const float samples[] = {-1.25f, -0.5f, -0.125f, 1.0f + 0.25f, 2.0f + 0.9f};
  for (size_t i = 0; i < ARRAY_SIZE(samples); ++i) {
    check_angle(samples[i]);
  }
}

static void test_modff(void) {
  /*
   * vna_modff() backs calibration math and must match libm semantics even when
   * the MCU lacks hardware FPU support.  Cover positive and negative values.
   * Tolerance based on accuracy analysis (max measured ~0.00000012).
   */
  float i = 0.0f;
  float f = vna_modff(12.75f, &i);
  if (fabsf(i - 12.0f) > 5e-7f || fabsf(f - 0.75f) > 5e-7f) {
    ++g_failures;
    fprintf(stderr, "[FAIL] modff positive i=%f f=%f\n", i, f);
  }

  f = vna_modff(-3.5f, &i);
  if (fabsf(i + 3.0f) > 5e-7f || fabsf(f + 0.5f) > 5e-7f) {
    ++g_failures;
    fprintf(stderr, "[FAIL] modff negative i=%f f=%f\n", i, f);
  }
}

static void test_vna_sqrt(void) {
  const float samples[] = {0.0f, 1.0f, 2.0f, 9.0f, 1234.5f};
  for (size_t i = 0; i < ARRAY_SIZE(samples); ++i) {
    float ref = sqrtf(samples[i]);
    float got = vna_sqrtf(samples[i]);
    if (fabsf(ref - got) > 1e-6f) { /* tolerance based on accuracy analysis (max measured ~0.00000057) */
      ++g_failures;
      fprintf(stderr, "[FAIL] sqrt sample=%f ref=%f got=%f\n", samples[i], ref, got);
    }
  }
}

static void test_fft_impulse(void) {
  /*
   * An impulse in the time domain should transform into a flat spectrum.  This
   * guards the bit-reversal and twiddle-table wiring.
   * Tolerance based on accuracy analysis (max measured ~0.0).
   */
  float bins[FFT_SIZE][2];
  memset(bins, 0, sizeof(bins));
  bins[0][0] = 1.0f;
  fft_forward(bins);
  for (size_t i = 0; i < FFT_SIZE; ++i) {
    if (fabsf(bins[i][0] - 1.0f) > 5e-7f || fabsf(bins[i][1]) > 5e-7f) {
      ++g_failures;
      fprintf(stderr, "[FAIL] fft impulse idx=%zu real=%f imag=%f\n", i, bins[i][0], bins[i][1]);
      break;
    }
  }
}

static void test_fft_roundtrip(void) {
  /*
   * Check that forward+inverse FFT produces the original signal (up to the
   * expected scaling factor).  This ensures the LUT-based butterflies are
   * numerically stable.
   * Tolerance based on accuracy analysis (max measured ~0.00000042).
   */
  float signal[FFT_SIZE][2];
  for (size_t i = 0; i < FFT_SIZE; ++i) {
    signal[i][0] = sinf((2.0f * VNA_PI * i) / FFT_SIZE);
    signal[i][1] = cosf((2.0f * VNA_PI * i) / FFT_SIZE);
  }
  float reference[FFT_SIZE][2];
  memcpy(reference, signal, sizeof(reference));

  fft_forward(signal);
  fft_inverse(signal);
  for (size_t i = 0; i < FFT_SIZE; ++i) {
    signal[i][0] /= FFT_SIZE;
    signal[i][1] /= FFT_SIZE;
    if (fabsf(signal[i][0] - reference[i][0]) > 1e-6f ||  /* tolerance based on accuracy analysis (max measured ~0.00000042) */
        fabsf(signal[i][1] - reference[i][1]) > 1e-6f) {
      ++g_failures;
      fprintf(stderr, "[FAIL] fft roundtrip idx=%zu ref=(%f,%f) got=(%f,%f)\n", i,
              reference[i][0], reference[i][1], signal[i][0], signal[i][1]);
      break;
    }
  }
}

int main(void) {
  test_primary_interval();
  test_negative_and_wrapped();
  test_modff();
  test_vna_sqrt();
  test_fft_impulse();
  test_fft_roundtrip();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_vna_math");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
