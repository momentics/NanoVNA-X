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
static THD_WORKING_AREA(waUIThread, 1536);

// Event mask for UI Wakeup
#define UI_WAKEUP_EVENT_MASK (eventmask_t)1

static thread_t* ui_thread_ptr = NULL;

void ui_task_signal(void) {
  if (ui_thread_ptr != NULL) {
    chEvtSignal(ui_thread_ptr, UI_WAKEUP_EVENT_MASK);
  }
}

static msg_t ui_task_entry(void* arg) {
  (void)arg;
  chRegSetThreadName("ui");
  ui_thread_ptr = chThdGetSelfX();

  // Initialize UI (moved from Thread1)
  ui_port.api->init();
  plot_init();
  
  while (true) {
    // Process UI events, touches, buttons
    ui_process();
    
    // Process Menu / Draw (if driven by polling in ui_process or if we need explicit draw)
    // The previous loop called ui_port.api->process() which mapped to ui_process()
    // and then called draw_all() ONLY if DEBUG_CONSOLE_SHOW was not set.
    
    #if !DEBUG_CONSOLE_SHOW
    // We need to call draw_all() here because it was in the main loop
    draw_all();
    #endif

    // Wait for event or timeout (for battery redraws or other periodic tasks)
    // 40ms timeout ensures ~25 FPS redraw rate without flooding
    chEvtWaitAnyTimeout(UI_WAKEUP_EVENT_MASK, MS2ST(40));
  }
  return MSG_OK;
}

void ui_task_init(void) {
  scheduler_start("ui", 
                  NORMALPRIO, // Higher priority than measurement (LOWPRIO)
                  waUIThread, 
                  sizeof(waUIThread), 
                  ui_task_entry, 
                  NULL);
}
