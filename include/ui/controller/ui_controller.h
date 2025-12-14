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

#include "ui/display/display_presenter.h"
#include "platform/boards/board_events.h"
#include "infra/event/event_bus.h"

typedef struct {
  board_events_t *board_events;
  const display_presenter_t *display;
  event_bus_t *config_events;
} ui_controller_port_t;

enum {
  UI_CONTROLLER_REQUEST_NONE = 0x00,
  UI_CONTROLLER_REQUEST_LEVER = 0x01,
  UI_CONTROLLER_REQUEST_TOUCH = 0x02,
  UI_CONTROLLER_REQUEST_CONSOLE = 0x04,
};

void ui_controller_configure(const ui_controller_port_t *port);
uint8_t ui_controller_pending_requests(void);
void ui_controller_release_requests(uint8_t mask);
void ui_controller_request_console_break(void);
