/*
 * Board-level event dispatcher that bridges hardware interrupts to the
 * platform event bus without heap allocations.
 */

#include "platform/boards/board_events.h"

#include "ch.h"

#define BOARD_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const board_touch_event_t touch_payloads[] = {
    [BOARD_TOUCH_SOURCE_ADC_WATCHDOG] = {.source = BOARD_TOUCH_SOURCE_ADC_WATCHDOG},
};

static const board_button_event_t button_payloads[16] = {
    {.channel = 0}, {.channel = 1}, {.channel = 2}, {.channel = 3},
    {.channel = 4}, {.channel = 5}, {.channel = 6}, {.channel = 7},
    {.channel = 8}, {.channel = 9}, {.channel = 10}, {.channel = 11},
    {.channel = 12}, {.channel = 13}, {.channel = 14}, {.channel = 15},
};

#if defined(NANOVNA_F303)

#define BOARD_EVENT_QUEUE_DEPTH 4U
#define BOARD_EVENT_THREAD_STACK 96
#define BOARD_EVENT_SUBSCRIPTIONS 3U
#define CCM_BSS __attribute__((section(".ram4_clear")))

static CCM_BSS event_bus_t board_bus;
static CCM_BSS event_bus_subscription_t board_slots[BOARD_EVENT_SUBSCRIPTIONS];
static CCM_BSS msg_t board_queue_storage[BOARD_EVENT_QUEUE_DEPTH];
static CCM_BSS event_bus_queue_node_t board_queue_nodes[BOARD_EVENT_QUEUE_DEPTH];
static THD_WORKING_AREA(waBoardEventWorker, BOARD_EVENT_THREAD_STACK) __attribute__((section(".ram4")));
static thread_t* board_event_thread = NULL;
static bool board_bus_ready = false;

static THD_FUNCTION(board_event_worker, arg) {
  event_bus_t* bus = (event_bus_t*)arg;
  chRegSetThreadName("board-evt");
  while (true) {
    if (bus == NULL || !bus->mailbox_ready || !bus->semaphore_ready) {
      chThdSleepMilliseconds(10);
      continue;
    }
    chBSemWait(&bus->semaphore);
    while (event_bus_dispatch(bus, TIME_IMMEDIATE)) {
    }
  }
}

void board_event_loop_init(void) {
  if (board_bus_ready) {
    return;
  }
  event_bus_init(&board_bus, board_slots, BOARD_ARRAY_SIZE(board_slots), board_queue_storage,
                 BOARD_ARRAY_SIZE(board_queue_storage), board_queue_nodes,
                 BOARD_ARRAY_SIZE(board_queue_nodes));
  board_bus_ready = true;
  board_event_thread = chThdCreateStatic(waBoardEventWorker, sizeof(waBoardEventWorker),
                                         NORMALPRIO - 2, board_event_worker, &board_bus);
  (void)board_event_thread;
}

event_bus_t* board_event_loop(void) {
  return board_bus_ready ? &board_bus : NULL;
}

bool board_event_subscribe(board_event_topic_t topic, event_bus_listener_t listener,
                           void* user_data) {
  if (!board_bus_ready) {
    return false;
  }
  return event_bus_subscribe(&board_bus, (event_bus_topic_t)topic, listener, user_data);
}

bool board_event_publish(board_event_topic_t topic, const void* payload) {
  if (!board_bus_ready) {
    return false;
  }
  return event_bus_publish(&board_bus, (event_bus_topic_t)topic, payload);
}

bool board_event_publish_from_isr(board_event_topic_t topic, const void* payload) {
  if (!board_bus_ready) {
    return false;
  }
  return event_bus_publish_from_isr(&board_bus, (event_bus_topic_t)topic, payload);
}

#else /* NANOVNA_F303 */

#define CCM_BSS
#define BOARD_EVENT_SUBSCRIPTIONS 2U

typedef struct {
  event_bus_listener_t listener;
  void* user_data;
  bool registered;
} board_event_slot_t;

static board_event_slot_t touch_slot = {0};
static board_event_slot_t button_slot = {0};

static void board_dispatch_event(board_event_topic_t topic, const void* payload) {
  board_event_slot_t* slot = (topic == BOARD_EVENT_TOUCH) ? &touch_slot : &button_slot;
  if (!slot->registered || slot->listener == NULL) {
    return;
  }
  event_bus_message_t message = {.topic = (event_bus_topic_t)topic, .payload = payload};
  slot->listener(&message, slot->user_data);
}

void board_event_loop_init(void) {
  touch_slot = (board_event_slot_t){0};
  button_slot = (board_event_slot_t){0};
}

event_bus_t* board_event_loop(void) {
  return NULL;
}

bool board_event_subscribe(board_event_topic_t topic, event_bus_listener_t listener,
                           void* user_data) {
  if (listener == NULL) {
    return false;
  }
  board_event_slot_t* slot = (topic == BOARD_EVENT_TOUCH) ? &touch_slot : &button_slot;
  if (slot->registered) {
    return false;
  }
  slot->listener = listener;
  slot->user_data = user_data;
  slot->registered = true;
  return true;
}

bool board_event_publish(board_event_topic_t topic, const void* payload) {
  board_dispatch_event(topic, payload);
  return true;
}

bool board_event_publish_from_isr(board_event_topic_t topic, const void* payload) {
  board_dispatch_event(topic, payload);
  return true;
}

#endif /* NANOVNA_F303 */

const board_touch_event_t* board_event_touch_payload(board_touch_source_t source) {
  if ((size_t)source >= BOARD_ARRAY_SIZE(touch_payloads)) {
    return NULL;
  }
  return &touch_payloads[source];
}

const board_button_event_t* board_event_button_payload(uint8_t channel) {
  if (channel >= BOARD_ARRAY_SIZE(button_payloads)) {
    return NULL;
  }
  return &button_payloads[channel];
}
