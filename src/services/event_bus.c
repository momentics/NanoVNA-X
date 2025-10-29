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

#include "services/event_bus.h"

void event_bus_init(event_bus_t* bus, event_bus_subscription_t* storage, size_t capacity) {
  if (bus == NULL) {
    return;
  }
  bus->subscriptions = storage;
  bus->capacity = storage ? capacity : 0;
  bus->count = 0;
}

bool event_bus_subscribe(event_bus_t* bus, event_bus_topic_t topic, event_bus_listener_t listener,
                         void* user_data) {
  if (bus == NULL || listener == NULL || bus->subscriptions == NULL) {
    return false;
  }
  if (bus->count >= bus->capacity) {
    return false;
  }
  event_bus_subscription_t* slot = &bus->subscriptions[bus->count++];
  slot->callback = listener;
  slot->user_data = user_data;
  slot->topic = topic;
  return true;
}

void event_bus_publish(event_bus_t* bus, event_bus_topic_t topic, const void* payload) {
  if (bus == NULL || bus->subscriptions == NULL) {
    return;
  }
  const event_bus_message_t message = {.topic = topic, .payload = payload};
  for (size_t i = 0; i < bus->count; ++i) {
    event_bus_subscription_t* slot = &bus->subscriptions[i];
    if (slot->callback == NULL) {
      continue;
    }
    if (slot->topic != topic) {
      continue;
    }
    slot->callback(&message, slot->user_data);
  }
}
