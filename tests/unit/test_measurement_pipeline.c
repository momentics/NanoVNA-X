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
 * Host-side unit tests for src/rf/pipeline/measurement_pipeline.c.  The file
 * contains only orchestration logic; by providing tiny stubs for the
 * app_measurement_* symbols we can validate its behaviour without STM32
 * hardware or the sweep engine.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "rf/pipeline.h"

static uint16_t g_mask_value = 0u;
static bool g_last_break = false;
static uint16_t g_last_channel = 0u;

uint16_t app_measurement_get_sweep_mask(void) {
  return g_mask_value;
}

bool app_measurement_sweep(bool break_on_operation, uint16_t channel_mask) {
  g_last_break = break_on_operation;
  g_last_channel = channel_mask;
  return (channel_mask & 0x1u) != 0u;
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
  bool completed = measurement_pipeline_execute(&pipeline, true, 0x03u);
  assert_true(completed == true, "stub returns true when LSB set");
  assert_true(g_last_break == true, "break flag propagated");
  assert_true(g_last_channel == 0x03u, "channel mask propagated");

  completed = measurement_pipeline_execute(&pipeline, false, 0x00u);
  assert_true(!completed, "stub returns false when LSB cleared");
  assert_true(g_last_break == false, "break flag updated");
  assert_true(g_last_channel == 0x00u, "channel mask updated");
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
