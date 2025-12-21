/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 */

#pragma once

#include "ch.h"

void ui_task_init(void);

// Initialize UI system (should be called from main thread)
void ui_task_system_init(void);

// Process UI events (should be called periodically from main loop)
void ui_task_process(void);

void ui_task_signal(void);

