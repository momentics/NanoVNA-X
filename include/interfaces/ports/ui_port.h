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

#include <stdint.h>

#include "nanovna.h"


typedef struct ui_module_port ui_module_port_t;

typedef struct ui_module_port_api {
  void (*init)(void);
  void (*process)(void);
  void (*enter_dfu)(void);
  void (*touch_cal_exec)(void);
  void (*touch_draw_test)(void);
  void (*message_box)(const char* header, const char* text, uint32_t delay_ms);
} ui_module_port_api_t;

struct ui_module_port {
  void* context;
  const ui_module_port_api_t* api;
};

extern const ui_module_port_api_t ui_port_api;
extern const ui_module_port_t ui_port;

