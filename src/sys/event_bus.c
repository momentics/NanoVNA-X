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

#include "sys/event_bus.h"

static bool event_bus_dispatch_to_subscribers(event_bus_t* bus, const event_bus_message_t* message) {
  bool handled = false;
  if (bus == NULL || message == NULL || bus->subscriptions == NULL) {
    return false;
  }
  for (size_t i = 0; i < bus->count; ++i) {
    event_bus_subscription_t* slot = &bus->subscriptions[i];
    if (slot->callback == NULL) {
      continue;
    }
    if (slot->topic != message->topic) {
      continue;
    }
    slot->callback(message, slot->user_data);
    handled = true;
  }
  return handled;
}

void event_bus_init(event_bus_t* bus, event_bus_subscription_t* storage, size_t capacity,
                    msg_t* queue_storage, size_t queue_length, event_bus_queue_node_t* nodes,
                    size_t node_count) {
  if (bus == NULL) {
    return;
  }
  bus->subscriptions = storage;
  bus->capacity = storage ? capacity : 0U;
  bus->count = 0U;
  bus->queue_storage = queue_storage;
  bus->queue_length = queue_length;
  bus->nodes = nodes;
  bus->node_count = node_count;
  bus->mailbox_ready = false;
  if (queue_storage != NULL && queue_length > 0U) {
    chMBObjectInit(&bus->mailbox, queue_storage, queue_length);
    bus->mailbox_ready = true;
  }
  if (nodes != NULL) {
    for (size_t i = 0; i < node_count; ++i) {
      nodes[i].in_use = false;
      nodes[i].message.topic = 0;
      nodes[i].message.payload = NULL;
    }
  }
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

static event_bus_queue_node_t* event_bus_alloc_node(event_bus_t* bus, event_bus_topic_t topic,
                                                    const void* payload) {
  if (bus == NULL || bus->nodes == NULL || bus->node_count == 0U) {
    return NULL;
  }

  event_bus_queue_node_t* node = NULL;
  for (size_t i = 0; i < bus->node_count; ++i) {
    event_bus_queue_node_t* candidate = &bus->nodes[i];
    if (!candidate->in_use) {
      candidate->in_use = true;
      candidate->message.topic = topic;
      candidate->message.payload = payload;
      node = candidate;
      break;
    }
  }
  return node;
}

static bool event_bus_enqueue(event_bus_t* bus, event_bus_queue_node_t* node, bool from_isr) {
  if (bus == NULL || node == NULL) {
    return false;
  }
  if (!bus->mailbox_ready) {
    return false;
  }

  msg_t msg = (msg_t)node;
  msg_t result;
  if (from_isr) {
    result = chMBPostI(&bus->mailbox, msg);
  } else {
    result = chMBPost(&bus->mailbox, msg, TIME_IMMEDIATE);
  }
  if (result != MSG_OK) {
    if (from_isr) {
      chSysLockFromISR();
      node->in_use = false;
      chSysUnlockFromISR();
    } else {
      chSysLock();
      node->in_use = false;
      chSysUnlock();
    }
    return false;
  }
  return true;
}

static bool event_bus_publish_common(event_bus_t* bus, event_bus_topic_t topic, const void* payload,
                                     bool from_isr) {
  if (bus == NULL) {
    return false;
  }

  if (!bus->mailbox_ready) {
    event_bus_message_t message = {.topic = topic, .payload = payload};
    event_bus_dispatch_to_subscribers(bus, &message);
    return true;
  }

  bool success = false;
  if (from_isr) {
    chSysLockFromISR();
  } else {
    chSysLock();
  }
  event_bus_queue_node_t* node = event_bus_alloc_node(bus, topic, payload);
  if (from_isr) {
    chSysUnlockFromISR();
  } else {
    chSysUnlock();
  }

  if (node == NULL) {
    return false;
  }

  success = event_bus_enqueue(bus, node, from_isr);
  if (!success && !from_isr) {
    event_bus_message_t message = {.topic = topic, .payload = payload};
    event_bus_dispatch_to_subscribers(bus, &message);
    success = true;
  }
  return success;
}

bool event_bus_publish(event_bus_t* bus, event_bus_topic_t topic, const void* payload) {
  return event_bus_publish_common(bus, topic, payload, false);
}

bool event_bus_publish_from_isr(event_bus_t* bus, event_bus_topic_t topic, const void* payload) {
  return event_bus_publish_common(bus, topic, payload, true);
}

bool event_bus_dispatch(event_bus_t* bus, systime_t timeout) {
  if (bus == NULL) {
    return false;
  }

  if (!bus->mailbox_ready) {
    (void)timeout;
    return false;
  }

  msg_t raw;
  msg_t result = chMBFetch(&bus->mailbox, &raw, timeout);
  if (result != MSG_OK) {
    return false;
  }

  event_bus_queue_node_t* node = (event_bus_queue_node_t*)raw;
  const event_bus_message_t message = node->message;
  event_bus_dispatch_to_subscribers(bus, &message);

  chSysLock();
  node->in_use = false;
  chSysUnlock();
  return true;
}
