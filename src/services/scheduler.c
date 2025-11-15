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

typedef struct scheduler_slot {
  thread_t* thread;
  scheduler_entry_t entry;
  void* user_data;
} scheduler_slot_t;

#if defined(NANOVNA_F303)
#define SCHEDULER_MAX_TASKS 4U
#else
#define SCHEDULER_MAX_TASKS 2U
#endif

static scheduler_slot_t scheduler_slots[SCHEDULER_MAX_TASKS];

static THD_FUNCTION(scheduler_entry_adapter, arg) {
  scheduler_slot_t* slot = (scheduler_slot_t*)arg;
  msg_t exit_code = MSG_OK;
  if (slot != NULL && slot->entry != NULL) {
    exit_code = slot->entry(slot->user_data);
  }

  chSysLock();
  slot->thread = NULL;
  slot->entry = NULL;
  slot->user_data = NULL;
  chSysUnlock();

  chThdExit(exit_code);
}

scheduler_task_t scheduler_start(const char* name, tprio_t priority, void* working_area,
                                 size_t working_area_size, scheduler_entry_t entry,
                                 void* user_data) {
  scheduler_task_t task = {.slot = NULL};
  if (entry == NULL || working_area == NULL || working_area_size == 0U) {
    return task;
  }

  scheduler_slot_t* slot = NULL;
  chSysLock();
  for (size_t i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
    if (scheduler_slots[i].thread == NULL) {
      slot = &scheduler_slots[i];
      slot->entry = entry;
      slot->user_data = user_data;
      break;
    }
  }
  chSysUnlock();

  if (slot == NULL) {
    return task;
  }

  thread_t* thread = chThdCreateStatic(working_area, working_area_size, priority,
                                       scheduler_entry_adapter, slot);
  if (thread == NULL) {
    chSysLock();
    slot->entry = NULL;
    slot->user_data = NULL;
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

  task.slot = slot;
  return task;
}

void scheduler_stop(scheduler_task_t* task) {
  if (task == NULL || task->slot == NULL) {
    return;
  }

  scheduler_slot_t* slot = task->slot;
  thread_t* thread;

  chSysLock();
  thread = slot->thread;
  chSysUnlock();

  if (thread == NULL) {
    task->slot = NULL;
    return;
  }

  chThdTerminate(thread);
#if CH_CFG_USE_WAITEXIT == TRUE
  chThdWait(thread);
#else
  while (!chThdTerminatedX(thread)) {
    chThdSleepMilliseconds(1);
  }
#endif

  chSysLock();
  slot->thread = NULL;
  slot->entry = NULL;
  slot->user_data = NULL;
  chSysUnlock();

  task->slot = NULL;
}
