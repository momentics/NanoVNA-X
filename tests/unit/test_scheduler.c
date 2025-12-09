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
 * Host-side regression tests for src/infra/task/scheduler.c.
 *
 * The cooperative task scheduler coordinates a small pool of worker slots on
 * the STM32.  By stubbing the ChibiOS primitives (thread creation/termination,
 * locks, and sleeps) we can exercise the slot allocator, saturated pool
 * behaviour, and graceful shutdown sequences entirely on a POSIX host.  Each
 * test intentionally mirrors a real-world failure mode (exhausted slots, failed
 * chThdCreateStatic, stopping an already-finished task) so that regressions are
 * caught in CI before they ever reach hardware.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "infra/task/scheduler.h"

/* ------------------------------------------------------------------------- */
/* Minimal ChibiOS primitives tailored for the scheduler tests.              */

#define STUB_MAX_THREADS 8

typedef struct {
  thread_t thread;
  bool in_use;
} stub_thread_slot_t;

static stub_thread_slot_t g_thread_pool[STUB_MAX_THREADS];
static thread_t* g_current_thread = NULL;
static bool g_force_create_failure = false;
static bool g_auto_run_threads = false;

static stub_thread_slot_t* stub_find_slot(thread_t* target) {
  if (target == NULL) {
    return NULL;
  }
  for (size_t i = 0; i < STUB_MAX_THREADS; ++i) {
    if (&g_thread_pool[i].thread == target) {
      return &g_thread_pool[i];
    }
  }
  return NULL;
}

static void stub_run_thread(thread_t* thread) {
  if (thread == NULL || thread->entry == NULL) {
    return;
  }
  g_current_thread = thread;
  thread->entry(thread->arg);
  g_current_thread = NULL;
  thread->terminated_flag = true;
}

void chSysLock(void) {}
void chSysUnlock(void) {}
void chSysLockFromISR(void) {}
void chSysUnlockFromISR(void) {}

thread_t* chThdCreateStatic(void* warea, size_t size, tprio_t prio, tfunc_t entry, void* arg) {
  (void)warea;
  (void)size;
  (void)prio;
  if (g_force_create_failure) {
    g_force_create_failure = false;
    return NULL;
  }
  for (size_t i = 0; i < STUB_MAX_THREADS; ++i) {
    stub_thread_slot_t* slot = &g_thread_pool[i];
    if (!slot->in_use) {
      slot->in_use = true;
      slot->thread.entry = entry;
      slot->thread.arg = arg;
      slot->thread.terminated_flag = false;
      if (g_auto_run_threads) {
        stub_run_thread(&slot->thread);
      }
      return &slot->thread;
    }
  }
  return NULL;
}

void chThdExit(msg_t msg) {
  (void)msg;
  if (g_current_thread == NULL) {
    return;
  }
  stub_thread_slot_t* slot = stub_find_slot(g_current_thread);
  if (slot != NULL) {
    slot->in_use = false;
    slot->thread.entry = NULL;
    slot->thread.arg = NULL;
  }
}

void chThdTerminate(thread_t* tp) {
  if (tp == NULL) {
    return;
  }
  stub_run_thread(tp);
}

void chThdWait(thread_t* tp) {
  (void)tp;
}

bool chThdTerminatedX(thread_t* tp) {
  if (tp == NULL) {
    return true;
  }
  return tp->terminated_flag;
}

void chThdSleepMilliseconds(uint32_t ms) {
  (void)ms;
}

/* ------------------------------------------------------------------------- */

static int g_failures = 0;

#define CHECK(cond)                                                                           \
  do {                                                                                        \
    if (!(cond)) {                                                                            \
      ++g_failures;                                                                           \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
    }                                                                                         \
  } while (0)

static msg_t counting_entry(void* user_data) {
  int* counter = (int*)user_data;
  if (counter != NULL) {
    (*counter)++;
  }
  return MSG_OK;
}

static void reset_stub_state(void) {
  memset(g_thread_pool, 0, sizeof(g_thread_pool));
  g_current_thread = NULL;
  g_force_create_failure = false;
  g_auto_run_threads = false;
}

static void test_start_and_stop_runs_entry(void) {
  reset_stub_state();
  uint8_t stack[128];
  int counter = 0;
  scheduler_task_t task = scheduler_start("worker", 5, stack, sizeof(stack), counting_entry,
                                          &counter);
  CHECK(task.slot != NULL);
  CHECK(counter == 0);
  scheduler_stop(&task);
  CHECK(counter == 1);
  CHECK(task.slot == NULL);
}

static void test_exhausts_slots_then_recovers(void) {
  reset_stub_state();
  uint8_t stacks[5][64];
  scheduler_task_t tasks[4];
  for (size_t i = 0; i < 4; ++i) {
    tasks[i] = scheduler_start("worker", 5, stacks[i], sizeof(stacks[i]), counting_entry, NULL);
    CHECK(tasks[i].slot != NULL);
  }
  scheduler_task_t overflow =
      scheduler_start("worker", 5, stacks[4], sizeof(stacks[4]), counting_entry, NULL);
  CHECK(overflow.slot == NULL);
  for (size_t i = 0; i < 4; ++i) {
    scheduler_stop(&tasks[i]);
    CHECK(tasks[i].slot == NULL);
  }
  scheduler_task_t recovered =
      scheduler_start("worker", 5, stacks[0], sizeof(stacks[0]), counting_entry, NULL);
  CHECK(recovered.slot != NULL);
  scheduler_stop(&recovered);
}

static void test_creation_failure_does_not_leak_slot(void) {
  reset_stub_state();
  uint8_t stack[64];
  scheduler_task_t first =
      scheduler_start("worker", 5, stack, sizeof(stack), counting_entry, NULL);
  CHECK(first.slot != NULL);

  g_force_create_failure = true;
  scheduler_task_t failed =
      scheduler_start("worker", 5, stack, sizeof(stack), counting_entry, NULL);
  CHECK(failed.slot == NULL);

  scheduler_task_t second =
      scheduler_start("worker", 5, stack, sizeof(stack), counting_entry, NULL);
  CHECK(second.slot != NULL);

  scheduler_stop(&first);
  scheduler_stop(&second);
}

static void test_stop_handles_completed_thread(void) {
  reset_stub_state();
  g_auto_run_threads = true; /* Threads exit before scheduler_stop observes them. */
  uint8_t stack[64];
  scheduler_task_t task =
      scheduler_start("oneshot", 5, stack, sizeof(stack), counting_entry, NULL);
  CHECK(task.slot != NULL);
  scheduler_stop(&task); /* Should be a no-op because slot->thread is already NULL. */
  CHECK(task.slot == NULL);
}

int main(void) {
  test_start_and_stop_runs_entry();
  test_exhausts_slots_then_recovers();
  test_creation_failure_does_not_leak_slot();
  test_stop_handles_completed_thread();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_scheduler");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
