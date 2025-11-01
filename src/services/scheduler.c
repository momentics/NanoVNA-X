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

#if defined(NANOVNA_F303)
#define SCHEDULER_HAS_STATIC_WA TRUE
#else
#define SCHEDULER_HAS_STATIC_WA FALSE
#endif

#if SCHEDULER_HAS_STATIC_WA
static THD_WORKING_AREA(default_wa, 512);
static scheduler_thread_context_t default_context;
#endif
static thread_t* default_thread = NULL;

#if SCHEDULER_HAS_STATIC_WA
static void scheduler_entry_adapter(void* arg) {
  scheduler_thread_context_t* context = (scheduler_thread_context_t*)arg;
  msg_t exit_code = MSG_OK;
  if (context != NULL && context->entry != NULL) {
    exit_code = context->entry(context->user_data);
  }
  chThdExit(exit_code);
}
#endif

scheduler_task_t scheduler_start(const char* name, tprio_t priority, size_t stack_size,
                                 scheduler_entry_t entry, void* user_data) {
  scheduler_task_t task = {.thread = NULL};
  if (entry == NULL) {
    return task;
  }
#if !SCHEDULER_HAS_STATIC_WA
  (void)name;
  (void)priority;
  (void)stack_size;
  (void)user_data;
  return task;
#else
  if ((stack_size != 0U && stack_size != sizeof(default_wa)) || default_thread != NULL) {
    return task;
  }
  default_context.entry = entry;
  default_context.user_data = user_data;

  task.thread = chThdCreateStatic(default_wa, sizeof(default_wa), priority, scheduler_entry_adapter,
                                  &default_context);
  if (task.thread == NULL) {
    return task;
  }
  default_thread = task.thread;
#if CH_CFG_USE_REGISTRY
  if (task.thread != NULL && name != NULL) {
    chRegSetThreadNameX(task.thread, name);
  }
#else
  (void)name;
#endif
  return task;
#endif
}

void scheduler_stop(scheduler_task_t* task) {
  if (task == NULL || task->thread == NULL) {
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
  if (task->thread == default_thread) {
    default_thread = NULL;
#if SCHEDULER_HAS_STATIC_WA
    default_context.entry = NULL;
    default_context.user_data = NULL;
#endif
  }
  task->thread = NULL;
}
