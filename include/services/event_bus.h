/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
 * Based on TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
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
#include "services/scheduler.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  EVENT_SWEEP_STARTED,
  EVENT_SWEEP_COMPLETED,
  EVENT_TOUCH_INPUT,
  EVENT_STORAGE_UPDATED,
  EVENT_CONFIGURATION_CHANGED,
  EVENT_SHELL_COMMAND_PENDING
} event_bus_topic_t;

typedef struct {
  event_bus_topic_t topic;
  const void* payload;
} event_bus_message_t;

typedef void (*event_bus_listener_t)(const event_bus_message_t* message, void* user_data);

typedef struct {
  event_bus_listener_t callback;
  void* user_data;
  event_bus_topic_t topic;
} event_bus_subscription_t;

#define EVENT_BUS_QUEUE_DEPTH 8U
#define EVENT_BUS_DISPATCH_STACK_DEPTH 384U
#define EVENT_BUS_DISPATCH_STACK_SIZE_BYTES \
  (sizeof(stkalign_t) * THD_WORKING_AREA_SIZE(EVENT_BUS_DISPATCH_STACK_DEPTH))

typedef struct {
  event_bus_message_t message;
  bool in_use;
} event_bus_queue_entry_t;

typedef struct event_bus {
  event_bus_subscription_t* subscriptions;
  size_t capacity;
  size_t count;
  mailbox_t mailbox;
  msg_t mailbox_buffer[EVENT_BUS_QUEUE_DEPTH];
  event_bus_queue_entry_t queue[EVENT_BUS_QUEUE_DEPTH];
  scheduler_task_t dispatcher_task;
} event_bus_t;

void event_bus_init(event_bus_t* bus, event_bus_subscription_t* storage, size_t capacity);

bool event_bus_subscribe(event_bus_t* bus, event_bus_topic_t topic, event_bus_listener_t listener,
                         void* user_data);

void event_bus_publish(event_bus_t* bus, event_bus_topic_t topic, const void* payload);

bool event_bus_publish_from_isr(event_bus_t* bus, event_bus_topic_t topic, const void* payload);
