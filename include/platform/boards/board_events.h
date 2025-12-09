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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  BOARD_EVENT_BUTTON = 0,
  BOARD_EVENT_TOUCH = 1,
  BOARD_EVENT_COUNT
} board_event_type_t;

typedef struct {
  board_event_type_t topic;
  union {
    struct {
      uint16_t channel;
    } button;
  } data;
} board_event_t;

typedef struct {
  board_event_type_t topic;
  void (*callback)(const board_event_t* event, void* user_data);
  void* user_data;
} board_event_subscription_t;

typedef void (*board_event_listener_t)(const board_event_t* event, void* user_data);

typedef struct {
  board_event_listener_t listeners[BOARD_EVENT_COUNT];
  void* listener_data[BOARD_EVENT_COUNT];
  uint16_t pending_channels[BOARD_EVENT_COUNT];
  uint8_t pending_counts[BOARD_EVENT_COUNT];
} board_events_t;

void board_events_init(board_events_t* events);

bool board_events_subscribe(board_events_t* events, board_event_type_t topic,
                            board_event_listener_t listener, void* user_data);

bool board_events_publish(board_events_t* events, const board_event_t* event);
bool board_events_publish_from_isr(board_events_t* events, const board_event_t* event);

bool board_events_dispatch(board_events_t* events);
uint32_t board_events_pending_mask(board_events_t* events);
