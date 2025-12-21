/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 */

#include "ui/core/ui_task.h"
#include "infra/task/scheduler.h"
#include "nanovna.h"
#include "interfaces/ports/ui_port.h"

// Stack size for UI thread. Needs to be sufficient for drawing operations.
// 1024 bytes should be safe for now, can be tuned later.
// static THD_WORKING_AREA(waUIThread, 1400); // Converted to Main Stack

// Event mask for UI Wakeup
#define UI_WAKEUP_EVENT_MASK (eventmask_t)1

static thread_t* ui_thread_ptr = NULL;

void ui_task_signal(void) {
  if (ui_thread_ptr != NULL) {
    chEvtSignal(ui_thread_ptr, UI_WAKEUP_EVENT_MASK);
  }
}

static systime_t last_ui_time = 0;

void ui_task_system_init(void) {
  // Initialize UI (moved from Thread1/UIThread)
  ui_port.api->init();
  plot_init();
  last_ui_time = chVTGetSystemTime();
}

void ui_task_process(void) {
  // Rate Limit UI to ~25Hz (40ms)
  // But allow immediate processing if events are pending?
  // For cooperative multitasking, we should check interval.
  
  // Always run input processing (it's fast and needs current state)
  ui_process();

  systime_t now = chVTGetSystemTime();
  if (now - last_ui_time >= MS2ST(40)) {
     last_ui_time = now;
     #if !DEBUG_CONSOLE_SHOW
     draw_all();
     #endif
  }
}

// Legacy Init (Unused now, but kept for signature compatibility if needed, or removed)
void ui_task_init(void) {
   // Deprecated in Single Thread Mode
   ui_task_system_init();
}
