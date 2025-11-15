/*
 * Unified board-level event bus for NanoVNA targets.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "services/event_bus.h"

typedef enum {
  BOARD_EVENT_TOUCH = EVENT_DRIVER_TOUCH_INTERRUPT,
  BOARD_EVENT_BUTTON = EVENT_DRIVER_BUTTON_INTERRUPT,
} board_event_topic_t;

typedef enum {
  BOARD_TOUCH_SOURCE_ADC_WATCHDOG = 0,
} board_touch_source_t;

typedef struct {
  board_touch_source_t source;
} board_touch_event_t;

typedef struct {
  uint8_t channel;
} board_button_event_t;

void board_event_loop_init(void);
event_bus_t* board_event_loop(void);

bool board_event_subscribe(board_event_topic_t topic, event_bus_listener_t listener, void* user_data);
bool board_event_publish(board_event_topic_t topic, const void* payload);
bool board_event_publish_from_isr(board_event_topic_t topic, const void* payload);

const board_touch_event_t* board_event_touch_payload(board_touch_source_t source);
const board_button_event_t* board_event_button_payload(uint8_t channel);
