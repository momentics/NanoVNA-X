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
 * Host-side unit tests for src/rf/pipeline.c.  The file
 * contains only orchestration logic; by providing tiny stubs for the
 * app_measurement_* and smoothing symbols we can validate its behaviour without STM32
 * hardware or the sweep engine.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "rf/pipeline.h"
#include "nanovna.h"
#include "rf/sweep.h"

static uint16_t g_mask_value = 0u;
static bool g_last_break = false;
static uint16_t g_last_channel = 0u;
static bool g_sweep_completed;
static unsigned g_sweep_calls;
static unsigned g_smooth_calls;
static uint16_t g_smooth_mask;
static unsigned g_sequence;
static unsigned g_sweep_sequence;
static unsigned g_smooth_sequence;

uint16_t app_measurement_get_sweep_mask(void) {
  return g_mask_value;
}

bool app_measurement_sweep(bool break_on_operation, uint16_t channel_mask) {
  g_last_break = break_on_operation;
  g_last_channel = channel_mask;
  ++g_sweep_calls;
  g_sweep_sequence = ++g_sequence;
  return g_sweep_completed;
}

void measurement_data_smooth(uint16_t channel_mask) {
  ++g_smooth_calls;
  g_smooth_mask = channel_mask;
  g_smooth_sequence = ++g_sequence;
}

static int g_failures = 0;

static void assert_true(bool cond, const char* msg) {
  if (!cond) {
    ++g_failures;
    fprintf(stderr, "[FAIL] %s\n", msg);
  }
}

static void test_init_and_mask(void) {
  measurement_pipeline_t pipeline = {.drivers = NULL};
  PlatformDrivers dummy = {0};
  measurement_pipeline_init(&pipeline, &dummy);
  assert_true(pipeline.drivers == &dummy, "init should capture driver table");

  g_mask_value = 0xAAu;
  uint16_t mask = measurement_pipeline_active_mask(&pipeline);
  assert_true(mask == 0xAAu, "active mask should proxy app_measurement_get_sweep_mask");
}

static void test_execute(void) {
  measurement_pipeline_t pipeline = {.drivers = NULL};
  const uint16_t masks[] = {0x00u, 0x01u, 0x02u, 0x03u, 0x80A5u};

  for (size_t i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) {
    for (unsigned complete = 0; complete < 2; ++complete) {
      g_sweep_completed = complete != 0;
      g_sweep_calls = 0;
      g_smooth_calls = 0;
      g_smooth_mask = 0u;
      g_sequence = 0;
      g_sweep_sequence = 0;
      g_smooth_sequence = 0;

      bool completed = measurement_pipeline_execute(&pipeline, !complete, masks[i]);
      assert_true(completed == g_sweep_completed, "sweep completion should propagate");
      assert_true(g_sweep_calls == 1, "pipeline should execute exactly one sweep");
      assert_true(g_last_break == !complete, "break flag should propagate");
      assert_true(g_last_channel == masks[i], "channel mask should propagate");
#ifdef __USE_SMOOTH__
      assert_true(g_smooth_calls == complete,
                  "only completed sweeps should smooth, exactly once");
      if (completed) {
        assert_true(g_smooth_mask == masks[i], "smoothing should receive the full channel mask");
        assert_true(g_sweep_sequence < g_smooth_sequence,
                    "smoothing should run after the sweep and before execute returns");
      }
#else
      assert_true(g_smooth_calls == 0, "disabled smoothing should never run");
#endif
    }
  }
}

int main(void) {
  test_init_and_mask();
  test_execute();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_measurement_pipeline");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
