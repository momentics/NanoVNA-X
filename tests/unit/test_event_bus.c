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

/*
 * Host-side regression tests for src/sys/event_bus.c.
 *
 * The firmware event bus uses mailbox-backed queues on the STM32 but can fall
 * back to synchronous dispatching when no queue is configured.  These tests
 * emulate both modes using lightweight ChibiOS stubs so we can verify FIFO
 * ordering, ISR-safe publishing, and node recycling entirely on a POSIX host.
 * Whenever a regression slips in (for example, queue nodes never being reused),
 * this suite fails deterministically during CI.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys/event_bus.h"

/* ------------------------------------------------------------------------- */
/* Minimal ChibiOS mailbox/syslock emulation used by the event bus during tests. */

static msg_t mailbox_push(mailbox_t* mbp, msg_t msg) {
  if (mbp == NULL || mbp->buffer == NULL || mbp->length == 0) {
    return MSG_TIMEOUT;
  }
  if (mbp->count >= mbp->length) {
    return MSG_TIMEOUT;
  }
  mbp->buffer[mbp->tail] = msg;
  mbp->tail = (mbp->tail + 1U) % mbp->length;
  ++mbp->count;
  return MSG_OK;
}

void chMBObjectInit(mailbox_t* mbp, msg_t* buf, size_t n) {
  if (mbp == NULL) {
    return;
  }
  mbp->buffer = buf;
  mbp->length = n;
  mbp->head = 0;
  mbp->tail = 0;
  mbp->count = 0;
}

msg_t chMBPost(mailbox_t* mbp, msg_t msg, systime_t timeout) {
  (void)timeout;
  return mailbox_push(mbp, msg);
}

msg_t chMBPostI(mailbox_t* mbp, msg_t msg) {
  return mailbox_push(mbp, msg);
}

msg_t chMBFetch(mailbox_t* mbp, msg_t* msgp, systime_t timeout) {
  (void)timeout;
  if (mbp == NULL || mbp->count == 0U) {
    return MSG_TIMEOUT;
  }
  if (msgp != NULL) {
    *msgp = mbp->buffer[mbp->head];
  }
  mbp->head = (mbp->head + 1U) % mbp->length;
  --mbp->count;
  return MSG_OK;
}

void chSysLock(void) {}
void chSysUnlock(void) {}
void chSysLockFromISR(void) {}
void chSysUnlockFromISR(void) {}

/* ------------------------------------------------------------------------- */

typedef struct {
  event_bus_topic_t topic;
  const char* payload_tag;
  uintptr_t user_token;
} event_record_t;

static event_record_t g_records[16];
static size_t g_record_count = 0;
static int g_failures = 0;

static void reset_records(void) {
  g_record_count = 0;
  memset(g_records, 0, sizeof(g_records));
}

static void recording_listener(const event_bus_message_t* message, void* user_data) {
  if (g_record_count >= sizeof(g_records) / sizeof(g_records[0])) {
    return;
  }
  event_record_t* rec = &g_records[g_record_count++];
  rec->topic = message->topic;
  rec->payload_tag = (const char*)message->payload;
  rec->user_token = (uintptr_t)user_data;
}

#define CHECK(cond)                                                                           \
  do {                                                                                        \
    if (!(cond)) {                                                                            \
      ++g_failures;                                                                           \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
    }                                                                                         \
  } while (0)

static void test_synchronous_publish_without_mailbox(void) {
  /*
   * When no mailbox storage is provided the bus must synchronously dispatch
   * every publish call (including the ISR variant) and preserve the order in
   * which listeners were registered.
   */
  event_bus_t bus;
  event_bus_subscription_t slots[4];
  event_bus_init(&bus, slots, 4, NULL, 0, NULL, 0);
  CHECK(bus.mailbox_ready == false);

  reset_records();
  CHECK(event_bus_subscribe(&bus, EVENT_SWEEP_STARTED, recording_listener, (void*)1));
  CHECK(event_bus_subscribe(&bus, EVENT_SWEEP_STARTED, recording_listener, (void*)2));

  CHECK(event_bus_publish(&bus, EVENT_SWEEP_STARTED, "sync0"));
  CHECK(event_bus_publish_from_isr(&bus, EVENT_SWEEP_STARTED, "sync1"));

  CHECK(g_record_count == 4);
  CHECK(strcmp(g_records[0].payload_tag, "sync0") == 0);
  CHECK(g_records[0].user_token == 1);
  CHECK(g_records[1].user_token == 2);
  CHECK(strcmp(g_records[2].payload_tag, "sync1") == 0);
  CHECK(g_records[2].user_token == 1);
  CHECK(g_records[3].user_token == 2);
}

static void test_queue_allocation_and_recycle(void) {
  /*
   * Provision a mailbox + node pool and verify that publish() enqueues events
   * in FIFO order, dispatch pops them correctly, and queue nodes are recycled
   * so the pool never exhausts once consumers keep draining the queue.
   */
  event_bus_t bus;
  event_bus_subscription_t slots[2];
  msg_t queue_storage[2];
  event_bus_queue_node_t nodes[2];
  event_bus_init(&bus, slots, 2, queue_storage, 2, nodes, 2);
  CHECK(bus.mailbox_ready == true);

  reset_records();
  CHECK(event_bus_subscribe(&bus, EVENT_SWEEP_COMPLETED, recording_listener, (void*)42));

  CHECK(event_bus_publish(&bus, EVENT_SWEEP_COMPLETED, "fifo0"));
  CHECK(event_bus_publish_from_isr(&bus, EVENT_SWEEP_COMPLETED, "fifo1"));
  CHECK(nodes[0].in_use || nodes[1].in_use);

  CHECK(event_bus_dispatch(&bus, TIME_IMMEDIATE));
  CHECK(g_record_count == 1);
  CHECK(strcmp(g_records[0].payload_tag, "fifo0") == 0);

  CHECK(event_bus_dispatch(&bus, TIME_IMMEDIATE));
  CHECK(g_record_count == 2);
  CHECK(strcmp(g_records[1].payload_tag, "fifo1") == 0);

  CHECK(nodes[0].in_use == false);
  CHECK(nodes[1].in_use == false);

  CHECK(event_bus_publish(&bus, EVENT_SWEEP_COMPLETED, "fifo2"));
  CHECK(event_bus_dispatch(&bus, TIME_IMMEDIATE));
  CHECK(g_record_count == 3);
  CHECK(strcmp(g_records[2].payload_tag, "fifo2") == 0);
}

int main(void) {
  test_synchronous_publish_without_mailbox();
  test_queue_allocation_and_recycle();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_event_bus");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
