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

#include "services/scheduler.h"

typedef struct {
  scheduler_entry_t entry;
  void* user_data;
} scheduler_thread_context_t;

typedef struct {
  thread_t* thread;
  scheduler_thread_context_t context;
  stkalign_t* work_area;
  size_t work_area_size;
} scheduler_slot_t;

static THD_FUNCTION(scheduler_entry_adapter, arg) {
  scheduler_thread_context_t* context = (scheduler_thread_context_t*)arg;
  msg_t exit_code = MSG_OK;
  if (context != NULL && context->entry != NULL) {
    exit_code = context->entry(context->user_data);
  }
  chThdExit(exit_code);
}

#if defined(NANOVNA_F303)
#define SCHEDULER_SLOT_COUNT 3U
static THD_WORKING_AREA(scheduler_wa0, 448);
static THD_WORKING_AREA(scheduler_wa1, 512);
static THD_WORKING_AREA(scheduler_wa2, 640);
static scheduler_slot_t scheduler_slots[SCHEDULER_SLOT_COUNT] = {
    {.thread = NULL, .context = {0}, .work_area = scheduler_wa0, .work_area_size = sizeof(scheduler_wa0)},
    {.thread = NULL, .context = {0}, .work_area = scheduler_wa1, .work_area_size = sizeof(scheduler_wa1)},
    {.thread = NULL, .context = {0}, .work_area = scheduler_wa2, .work_area_size = sizeof(scheduler_wa2)},
};
#else
#define SCHEDULER_SLOT_COUNT 2U
static THD_WORKING_AREA(scheduler_wa0, 320);
static THD_WORKING_AREA(scheduler_wa1, 384);
static scheduler_slot_t scheduler_slots[SCHEDULER_SLOT_COUNT] = {
    {.thread = NULL, .context = {0}, .work_area = scheduler_wa0, .work_area_size = sizeof(scheduler_wa0)},
    {.thread = NULL, .context = {0}, .work_area = scheduler_wa1, .work_area_size = sizeof(scheduler_wa1)},
};
#endif

static scheduler_slot_t* scheduler_acquire_slot(size_t stack_size) {
  scheduler_slot_t* selected = NULL;
  for (size_t i = 0; i < SCHEDULER_SLOT_COUNT; ++i) {
    scheduler_slot_t* slot = &scheduler_slots[i];
    if (slot->thread != NULL) {
      continue;
    }
    if (stack_size != 0U && stack_size > slot->work_area_size) {
      continue;
    }
    selected = slot;
    break;
  }
  if (selected != NULL) {
    selected->thread = (thread_t*)1; // reserve slot
  }
  return selected;
}

static void scheduler_release_slot(scheduler_slot_t* slot) {
  if (slot == NULL) {
    return;
  }
  slot->thread = NULL;
  slot->context.entry = NULL;
  slot->context.user_data = NULL;
}

static scheduler_slot_t* scheduler_find_slot(thread_t* thread) {
  if (thread == NULL) {
    return NULL;
  }
  for (size_t i = 0; i < SCHEDULER_SLOT_COUNT; ++i) {
    if (scheduler_slots[i].thread == thread) {
      return &scheduler_slots[i];
    }
  }
  return NULL;
}

scheduler_task_t scheduler_start(const char* name, tprio_t priority, size_t stack_size,
                                 scheduler_entry_t entry, void* user_data) {
  scheduler_task_t task = {.thread = NULL};
  if (entry == NULL) {
    return task;
  }

  chSysLock();
  scheduler_slot_t* slot = scheduler_acquire_slot(stack_size);
  chSysUnlock();
  if (slot == NULL) {
    return task;
  }

  slot->context.entry = entry;
  slot->context.user_data = user_data;

  thread_t* thread = chThdCreateStatic(slot->work_area, slot->work_area_size, priority,
                                       scheduler_entry_adapter, &slot->context);
  if (thread == NULL) {
    chSysLock();
    scheduler_release_slot(slot);
    chSysUnlock();
    return task;
  }

  chSysLock();
  slot->thread = thread;
  chSysUnlock();

#if CH_CFG_USE_REGISTRY
  if (name != NULL) {
    chRegSetThreadNameX(thread, name);
  }
#else
  (void)name;
#endif

  task.thread = thread;
  return task;
}

void scheduler_stop(scheduler_task_t* task) {
  if (task == NULL || task->thread == NULL) {
    return;
  }
  scheduler_slot_t* slot = NULL;
  chSysLock();
  slot = scheduler_find_slot(task->thread);
  chSysUnlock();
  if (slot == NULL) {
    return;
  }

  chThdTerminate(task->thread);
#if CH_CFG_USE_WAITEXIT == TRUE
  chThdWait(task->thread);
#else
  while (!chThdTerminatedX(task->thread)) {
    chThdSleepMilliseconds(1);
  }
#endif

  chSysLock();
  scheduler_release_slot(slot);
  chSysUnlock();

  task->thread = NULL;
}
