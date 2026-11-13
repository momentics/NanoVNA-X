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
 * Regression tests for the RF measurement engine.
 *
 * The production path orchestrates the measurement pipeline, sweep service,
 * and the UI-facing event bus.  By substituting each dependency with lightweight
 * host stubs we can exercise the state machine deterministically: sweeps are
 * triggered via a fake port, the real pipeline calls a boolean sweep stub,
 * and the sweep service records call counts and ordering.  These tests ensure
 * EVENT_SWEEP_{STARTED,COMPLETED} fire in the right order, that break flags
 * propagate down to the pipeline, and that results always reach the port even
 * when a sweep aborts.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rf/measurement.h"
#include "nanovna.h"
#include "rf/sweep.h"

/* ------------------------------------------------------------------------- */
/*                         Stubbed sweep service hooks                        */

static int g_sweep_init_calls;
static event_bus_t* g_sweep_init_bus;
static int g_sweep_begin_calls;
static int g_sweep_end_calls;
static int g_sweep_generation_calls;
static unsigned g_sequence;
static unsigned g_sweep_sequence;
static unsigned g_smooth_sequence;
static unsigned g_end_sequence;
static unsigned g_generation_sequence;
static unsigned g_result_sequence;

void sweep_service_init(event_bus_t* bus) {
  ++g_sweep_init_calls;
  g_sweep_init_bus = bus;
}

void sweep_service_begin_measurement(void) {
  ++g_sweep_begin_calls;
}

void sweep_service_end_measurement(void) {
  ++g_sweep_end_calls;
  g_end_sequence = ++g_sequence;
}

uint32_t sweep_service_increment_generation(void) {
  ++g_sweep_generation_calls;
  g_generation_sequence = ++g_sequence;
  return (uint32_t)g_sweep_generation_calls;
}

/* ------------------------------------------------------------------------- */
/*                         Measurement pipeline stubs                         */

static uint16_t g_active_mask = 0x01u;
static bool g_pipeline_result = true;
static bool g_last_break_flag = false;
static uint16_t g_last_pipeline_mask = 0;
static int g_smooth_calls;
static uint16_t g_smooth_mask;

uint16_t app_measurement_get_sweep_mask(void) {
  return g_active_mask;
}

bool app_measurement_sweep(bool break_on_operation, uint16_t channel_mask) {
  g_last_break_flag = break_on_operation;
  g_last_pipeline_mask = channel_mask;
  g_sweep_sequence = ++g_sequence;
  return g_pipeline_result;
}

void measurement_data_smooth(uint16_t channel_mask) {
  ++g_smooth_calls;
  g_smooth_mask = channel_mask;
  g_smooth_sequence = ++g_sequence;
}

/* ------------------------------------------------------------------------- */
/*                             Event bus stubs                                */

typedef struct {
  event_bus_topic_t topic;
  uint16_t mask;
  unsigned sequence;
} recorded_event_t;

static recorded_event_t g_recorded_events[4];
static size_t g_recorded_event_count = 0;

bool event_bus_publish(event_bus_t* bus, event_bus_topic_t topic, const void* payload) {
  (void)bus;
  if (g_recorded_event_count >= sizeof(g_recorded_events) / sizeof(g_recorded_events[0])) {
    return true;
  }
  recorded_event_t* rec = &g_recorded_events[g_recorded_event_count++];
  rec->topic = topic;
  rec->mask = payload ? *(const uint16_t*)payload : 0;
  rec->sequence = ++g_sequence;
  return true;
}

/* ------------------------------------------------------------------------- */
/*                             OS timing stubs                                */

static int g_sleep_calls = 0;

void chThdSleepMilliseconds(uint32_t ms) {
  (void)ms;
  ++g_sleep_calls;
}

/* ------------------------------------------------------------------------- */

typedef struct {
  bool allow_start;
  bool next_break_flag;
  int service_calls;
  int can_start_calls;
  int handle_result_calls;
  measurement_engine_result_t last_result;
} fake_port_state_t;

static void fake_port_service_loop(measurement_engine_port_t* port) {
  fake_port_state_t* state = (fake_port_state_t*)port->context;
  ++state->service_calls;
}

static bool fake_port_can_start(measurement_engine_port_t* port,
                                measurement_engine_request_t* request) {
  fake_port_state_t* state = (fake_port_state_t*)port->context;
  ++state->can_start_calls;
  request->break_on_operation = state->next_break_flag;
  return state->allow_start;
}

static void fake_port_handle_result(measurement_engine_port_t* port,
                                    const measurement_engine_result_t* result) {
  fake_port_state_t* state = (fake_port_state_t*)port->context;
  ++state->handle_result_calls;
  g_result_sequence = ++g_sequence;
  if (result != NULL) {
    state->last_result = *result;
  }
}

static measurement_engine_port_t g_fake_port = {
    .context = NULL,
    .can_start_sweep = fake_port_can_start,
    .handle_result = fake_port_handle_result,
    .service_loop = fake_port_service_loop,
};

static void reset_stubs(void) {
  g_sweep_init_calls = 0;
  g_sweep_init_bus = NULL;
  g_sweep_begin_calls = 0;
  g_sweep_end_calls = 0;
  g_sweep_generation_calls = 0;
  g_sequence = 0;
  g_sweep_sequence = 0;
  g_smooth_sequence = 0;
  g_end_sequence = 0;
  g_generation_sequence = 0;
  g_result_sequence = 0;
  g_active_mask = 0x0Fu;
  g_pipeline_result = true;
  g_last_break_flag = false;
  g_last_pipeline_mask = 0;
  g_smooth_calls = 0;
  g_smooth_mask = 0;
  g_recorded_event_count = 0;
  memset(g_recorded_events, 0, sizeof(g_recorded_events));
  g_sleep_calls = 0;
}

static int g_failures = 0;

#define CHECK(cond, msg)                                                                         \
  do {                                                                                           \
    if (!(cond)) {                                                                               \
      ++g_failures;                                                                              \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg);                            \
    }                                                                                            \
  } while (0)

static void test_init_calls_sweep_service(void) {
  reset_stubs();
  measurement_engine_t engine;
  memset(&engine, 0, sizeof(engine));
  fake_port_state_t state = {.allow_start = false, .next_break_flag = true};
  g_fake_port.context = &state;
  PlatformDrivers drivers = {0};
  event_bus_t bus = {0};

  measurement_engine_init(&engine, &g_fake_port, &bus, &drivers);
  CHECK(g_sweep_init_calls == 1, "sweep_service_init should run exactly once");
  CHECK(g_sweep_init_bus == &bus, "sweep service should receive the event bus");
  CHECK(engine.port == &g_fake_port, "engine should track the provided port");
  CHECK(engine.event_bus == &bus, "engine should retain the event bus pointer");
  CHECK(engine.pipeline.drivers == &drivers, "pipeline should see driver table");
}

static void test_tick_null_engine_sleeps(void) {
  reset_stubs();
  measurement_engine_tick(NULL);
  CHECK(g_sleep_calls == 1, "null engine should sleep once");
  CHECK(g_smooth_calls == 0, "null engine should not smooth");
}

static void test_tick_without_trigger_sleeps_and_skips_events(void) {
  reset_stubs();
  measurement_engine_t engine;
  memset(&engine, 0, sizeof(engine));
  fake_port_state_t state = {.allow_start = false, .next_break_flag = false};
  g_fake_port.context = &state;
  PlatformDrivers drivers = {0};

  measurement_engine_init(&engine, &g_fake_port, NULL, &drivers);
  measurement_engine_tick(&engine);

  CHECK(state.service_calls == 1, "service loop should run even without sweeps");
  CHECK(state.can_start_calls == 1, "port should be queried for sweep permission");
  CHECK(g_sleep_calls == 1, "engine should sleep when idle");
  CHECK(g_recorded_event_count == 0, "no events must be published when idle");
  CHECK(g_sweep_begin_calls == 0, "sweep helpers must not run when idle");
  CHECK(g_sweep_init_bus == NULL, "sweep service should accept a null event bus");
  CHECK(g_smooth_calls == 0, "idle engine should not smooth");
  CHECK(state.handle_result_calls == 0, "idle engine should not report a result");
}

static void test_tick_completed_sweep_publishes_events(void) {
  reset_stubs();
  measurement_engine_t engine;
  memset(&engine, 0, sizeof(engine));
  fake_port_state_t state = {.allow_start = true, .next_break_flag = false};
  g_fake_port.context = &state;
  PlatformDrivers drivers = {0};
  event_bus_t bus = {0};
  measurement_engine_init(&engine, &g_fake_port, &bus, &drivers);

  g_active_mask = 0xA5u;
  g_pipeline_result = true;
  state.next_break_flag = false;
  measurement_engine_tick(&engine);

  CHECK(g_sweep_begin_calls == 1, "sweep begin must run");
  CHECK(g_sweep_end_calls == 1, "sweep end must run");
  CHECK(g_sweep_generation_calls == 1, "generation counter must increment");
  CHECK(g_recorded_event_count == 2, "two events expected for a completed sweep");
  CHECK(g_recorded_events[0].topic == EVENT_SWEEP_STARTED, "first event must be STARTED");
  CHECK(g_recorded_events[1].topic == EVENT_SWEEP_COMPLETED, "second event must be COMPLETED");
  CHECK(g_recorded_events[0].mask == 0, "queued event must not borrow stack payload");
  CHECK(g_recorded_events[1].mask == 0, "queued event must not borrow stack payload");
  CHECK(g_last_break_flag == state.next_break_flag, "break flag must reach pipeline");
  CHECK(g_last_pipeline_mask == g_active_mask, "pipeline must see current sweep mask");
  CHECK(state.handle_result_calls == 1, "port callback should run");
  CHECK(state.last_result.completed == true, "result must reflect sweep completion");
  CHECK(state.last_result.sweep_mask == g_active_mask, "result should include sweep mask");
  CHECK(g_recorded_events[0].sequence < g_sweep_sequence, "STARTED must precede the sweep");
#ifdef __USE_SMOOTH__
  CHECK(g_smooth_calls == 1, "completed sweep must smooth exactly once");
  CHECK(g_smooth_mask == g_active_mask, "smoothing must receive the complete sweep mask");
  CHECK(g_sweep_sequence < g_smooth_sequence, "smoothing must follow the sweep");
  CHECK(g_smooth_sequence < g_end_sequence, "smoothing must finish before measurement ends");
  CHECK(g_smooth_sequence < g_generation_sequence, "smoothing must precede generation increment");
  CHECK(g_smooth_sequence < g_result_sequence, "smoothing must precede the result callback");
#else
  CHECK(g_smooth_calls == 0, "disabled smoothing must not run");
#endif
  CHECK(g_sweep_sequence < g_end_sequence, "measurement must end after the sweep");
  CHECK(g_end_sequence < g_generation_sequence, "measurement must end before generation increment");
  CHECK(g_generation_sequence < g_recorded_events[1].sequence,
        "generation increment must precede COMPLETED");
  CHECK(g_result_sequence < g_recorded_events[1].sequence, "postprocessing must precede COMPLETED");

  g_active_mask = 0x02u;
  measurement_engine_tick(&engine);
#ifdef __USE_SMOOTH__
  CHECK(g_smooth_calls == 2, "each completed sweep must smooth exactly once");
  CHECK(g_smooth_mask == g_active_mask, "smoothing must use the new sweep mask");
  CHECK(g_sweep_sequence < g_smooth_sequence, "next smoothing must follow the next sweep");
  CHECK(g_smooth_sequence < g_generation_sequence, "next generation must follow smoothing");
  CHECK(g_smooth_sequence < g_result_sequence, "next result callback must follow smoothing");
#else
  CHECK(g_smooth_calls == 0, "disabled smoothing must remain unused on later sweeps");
#endif
  CHECK(g_sweep_generation_calls == 2, "each completed sweep must increment generation once");
  CHECK(state.handle_result_calls == 2, "each completed sweep must report a result once");
  CHECK(state.last_result.sweep_mask == g_active_mask, "next result must use the new mask");
}

static void test_tick_incomplete_sweep_skips_completed_event(void) {
  reset_stubs();
  measurement_engine_t engine;
  memset(&engine, 0, sizeof(engine));
  fake_port_state_t state = {.allow_start = true, .next_break_flag = true};
  g_fake_port.context = &state;
  PlatformDrivers drivers = {0};
  event_bus_t bus = {0};
  measurement_engine_init(&engine, &g_fake_port, &bus, &drivers);

  g_active_mask = 0x55u;
  g_pipeline_result = false;
  measurement_engine_tick(&engine);

  CHECK(g_recorded_event_count == 1, "only STARTED event should fire on failure");
  CHECK(g_recorded_events[0].topic == EVENT_SWEEP_STARTED, "first event must be STARTED");
  CHECK(g_sweep_generation_calls == 0, "generation must not bump on failure");
  CHECK(g_smooth_calls == 0, "partial sweep must never smooth");
  CHECK(g_sweep_begin_calls == 1, "partial sweep must begin measurement once");
  CHECK(g_sweep_end_calls == 1, "partial sweep must end measurement once");
  CHECK(g_sweep_sequence < g_end_sequence, "partial measurement must end after the sweep");
  CHECK(g_end_sequence < g_result_sequence, "partial result must follow measurement end");
  CHECK(state.handle_result_calls == 1, "partial sweep must report its result once");
  CHECK(g_last_break_flag == state.next_break_flag, "partial sweep must retain the break flag");
  CHECK(g_last_pipeline_mask == g_active_mask, "partial sweep must retain the channel mask");
  CHECK(state.last_result.completed == false, "port must learn about the failure");
  CHECK(state.last_result.sweep_mask == g_active_mask, "result mask should still propagate");
}

int main(void) {
  test_init_calls_sweep_service();
  test_tick_null_engine_sleeps();
  test_tick_without_trigger_sleeps_and_skips_events();
  test_tick_completed_sweep_publishes_events();
  test_tick_incomplete_sweep_skips_completed_event();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_measurement_engine");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
