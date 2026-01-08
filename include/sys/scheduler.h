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

#pragma once

#include "ch.h"

typedef msg_t (*scheduler_entry_t)(void* user_data);

struct scheduler_slot;

typedef struct {
  struct scheduler_slot* slot;
} scheduler_task_t;

scheduler_task_t scheduler_start(const char* name, tprio_t priority, void* working_area,
                                 size_t working_area_size, scheduler_entry_t entry,
                                 void* user_data);

void scheduler_stop(scheduler_task_t* task);
