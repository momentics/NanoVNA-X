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

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  EVENT_SWEEP_STARTED,
  EVENT_SWEEP_COMPLETED,
  EVENT_TOUCH_INPUT,
  EVENT_STORAGE_UPDATED,
  EVENT_CONFIGURATION_CHANGED
} event_bus_topic_t;

typedef struct {
  event_bus_topic_t topic;
  const void *payload;
} event_bus_message_t;

typedef void (*event_bus_listener_t)(const event_bus_message_t *message, void *user_data);

typedef struct {
  event_bus_listener_t callback;
  void *user_data;
  event_bus_topic_t topic;
} event_bus_subscription_t;

typedef struct {
  event_bus_subscription_t *subscriptions;
  size_t capacity;
  size_t count;
} event_bus_t;

void event_bus_init(event_bus_t *bus,
                    event_bus_subscription_t *storage,
                    size_t capacity);

bool event_bus_subscribe(event_bus_t *bus,
                         event_bus_topic_t topic,
                         event_bus_listener_t listener,
                         void *user_data);

void event_bus_publish(event_bus_t *bus,
                       event_bus_topic_t topic,
                       const void *payload);
