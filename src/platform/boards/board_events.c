#include "platform/boards/board_events.h"

void board_events_init(board_events_t* events) {
  if (events == NULL) {
    return;
  }
  for (size_t i = 0; i < BOARD_EVENT_COUNT; ++i) {
    events->listeners[i] = NULL;
    events->listener_data[i] = NULL;
    events->pending_channels[i] = 0;
    events->pending_counts[i] = 0;
  }
}

bool board_events_subscribe(board_events_t* events, board_event_type_t topic,
                            board_event_listener_t listener, void* user_data) {
  if (events == NULL || listener == NULL) {
    return false;
  }
  if (topic >= BOARD_EVENT_COUNT) {
    return false;
  }
  events->listeners[topic] = listener;
  events->listener_data[topic] = user_data;
  return true;
}

static void board_events_store_event(board_events_t* events, const board_event_t* event) {
  if (events == NULL || event == NULL) {
    return;
  }
  board_event_type_t topic = event->topic;
  if (topic >= BOARD_EVENT_COUNT) {
    return;
  }
  events->pending_channels[topic] = event->data.button.channel;
  if (events->pending_counts[topic] < UINT8_MAX) {
    events->pending_counts[topic]++;
  }
}

bool board_events_publish(board_events_t* events, const board_event_t* event) {
  chSysLock();
  board_events_store_event(events, event);
  chSysUnlock();
  return true;
}

bool board_events_publish_from_isr(board_events_t* events, const board_event_t* event) {
  chSysLockFromISR();
  board_events_store_event(events, event);
  chSysUnlockFromISR();
  return true;
}

bool board_events_dispatch(board_events_t* events) {
  if (events == NULL) {
    return false;
  }
  bool dispatched = false;
  for (size_t i = 0; i < BOARD_EVENT_COUNT; ++i) {
    chSysLock();
    uint16_t count = events->pending_counts[i];
    if (count > 0U) {
      events->pending_counts[i]--;
    }
    chSysUnlock();
    if (count == 0U) {
      continue;
    }
    board_event_listener_t listener = events->listeners[i];
    if (listener != NULL) {
      board_event_t event = {.topic = (board_event_type_t)i};
      event.data.button.channel = events->pending_channels[i];
      listener(&event, events->listener_data[i]);
      dispatched = true;
    }
  }
  return dispatched;
}

uint32_t board_events_pending_mask(board_events_t* events) {
  if (events == NULL) {
    return 0;
  }
  uint32_t mask = 0;
  chSysLock();
  for (size_t i = 0; i < BOARD_EVENT_COUNT; ++i) {
    if (events->pending_counts[i] > 0U) {
      mask |= (1U << i);
    }
  }
  chSysUnlock();
  return mask;
}
