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

#if EVENT_BUS_ENABLE_DISPATCHER
static event_bus_queue_entry_t* event_bus_alloc_slot_s(event_bus_t* bus) {
  chDbgCheckClassS();
  for (size_t i = 0; i < EVENT_BUS_QUEUE_DEPTH; ++i) {
    event_bus_queue_entry_t* slot = &bus->queue[i];
    if (!slot->in_use) {
      slot->in_use = true;
      return slot;
    }
  }
  return NULL;
}

static void event_bus_release_slot_s(event_bus_queue_entry_t* slot) {
  chDbgCheckClassS();
  slot->in_use = false;
}
#endif

static void event_bus_dispatch(event_bus_t* bus, const event_bus_message_t* message) {
  for (size_t i = 0; i < bus->count; ++i) {
    event_bus_subscription_t* slot = &bus->subscriptions[i];
    if (slot->callback == NULL || slot->topic != message->topic) {
      continue;
    }
    slot->callback(message, slot->user_data);
  }
}

#if EVENT_BUS_ENABLE_DISPATCHER
static msg_t event_bus_dispatcher_entry(void* arg) {
  event_bus_t* bus = (event_bus_t*)arg;
  while (true) {
    event_bus_queue_entry_t* slot = NULL;
    if (chMBFetch(&bus->mailbox, (msg_t*)&slot, TIME_INFINITE) != MSG_OK) {
      continue;
    }
    if (slot == NULL) {
      continue;
    }
    event_bus_message_t message = slot->message;
    chSysLock();
    event_bus_release_slot_s(slot);
    chSysUnlock();
    event_bus_dispatch(bus, &message);
  }
  return MSG_OK;
}
#endif

void event_bus_init(event_bus_t* bus, event_bus_subscription_t* storage, size_t capacity) {
  if (bus == NULL) {
    return;
  }
  bus->subscriptions = storage;
  bus->capacity = storage ? capacity : 0U;
  bus->count = 0U;
#if EVENT_BUS_ENABLE_DISPATCHER
  for (size_t i = 0; i < EVENT_BUS_QUEUE_DEPTH; ++i) {
    bus->queue[i].in_use = false;
    bus->queue[i].message.topic = EVENT_SWEEP_STARTED;
    bus->queue[i].message.payload = NULL;
  }
  chMBObjectInit(&bus->mailbox, bus->mailbox_buffer, EVENT_BUS_QUEUE_DEPTH);
  bus->dispatcher_task =
      scheduler_start("event-bus", NORMALPRIO - 1, EVENT_BUS_DISPATCH_STACK_SIZE_BYTES,
                      event_bus_dispatcher_entry, bus);
#else
  bus->dispatcher_task.thread = NULL;
#endif
}

bool event_bus_subscribe(event_bus_t* bus, event_bus_topic_t topic, event_bus_listener_t listener,
                         void* user_data) {
  if (bus == NULL || listener == NULL || bus->subscriptions == NULL || bus->count >= bus->capacity) {
    return false;
  }
  event_bus_subscription_t* slot = &bus->subscriptions[bus->count++];
  slot->callback = listener;
  slot->user_data = user_data;
  slot->topic = topic;
  return true;
}

static bool event_bus_post(event_bus_t* bus, event_bus_topic_t topic, const void* payload) {
  if (bus == NULL || bus->subscriptions == NULL) {
    return false;
  }
#if !EVENT_BUS_ENABLE_DISPATCHER
  const event_bus_message_t message = {.topic = topic, .payload = payload};
  event_bus_dispatch(bus, &message);
  return true;
#else
  if (bus->dispatcher_task.thread == NULL) {
    const event_bus_message_t message = {.topic = topic, .payload = payload};
    event_bus_dispatch(bus, &message);
    return true;
  }
  bool enqueued = false;
  chSysLock();
  event_bus_queue_entry_t* slot = event_bus_alloc_slot_s(bus);
  if (slot != NULL) {
    slot->message.topic = topic;
    slot->message.payload = payload;
    enqueued = true;
  }
  chSysUnlock();
  if (!enqueued) {
    return false;
  }
  msg_t result = chMBPost(&bus->mailbox, (msg_t)slot, TIME_IMMEDIATE);
  if (result != MSG_OK) {
    chSysLock();
    event_bus_release_slot_s(slot);
    chSysUnlock();
    return false;
  }
  return true;
#endif
}

void event_bus_publish(event_bus_t* bus, event_bus_topic_t topic, const void* payload) {
  (void)event_bus_post(bus, topic, payload);
}

bool event_bus_publish_from_isr(event_bus_t* bus, event_bus_topic_t topic, const void* payload) {
#if !EVENT_BUS_ENABLE_DISPATCHER
  (void)bus;
  (void)topic;
  (void)payload;
  return false;
#else
  if (bus == NULL || bus->subscriptions == NULL) {
    return false;
  }
  chSysLockFromISR();
  event_bus_queue_entry_t* slot = event_bus_alloc_slot_s(bus);
  if (slot == NULL) {
    chSysUnlockFromISR();
    return false;
  }
  slot->message.topic = topic;
  slot->message.payload = payload;
  msg_t result = chMBPostI(&bus->mailbox, (msg_t)slot);
  if (result != MSG_OK) {
    event_bus_release_slot_s(slot);
    chSysUnlockFromISR();
    return false;
  }
  chSysUnlockFromISR();
  return true;
#endif
}
