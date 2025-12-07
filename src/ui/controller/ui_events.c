/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
 * All rights reserved.
 */
#include "nanovna.h"
#include "ui/controller/ui_events.h"
#include "infra/event/event_bus.h"
#include "platform/boards/board_events.h"
#include "ui/display/display_presenter.h"
#include "ui/ui_internal.h"
#include "hal.h" // For chSysLock/Unlock

static event_bus_t* ui_event_bus = NULL;
static board_events_t* ui_board_events = NULL;
static bool ui_board_events_subscribed = false;
static uint8_t ui_request_flags = UI_CONTROLLER_REQUEST_NONE;

static void ui_on_event(const event_bus_message_t* message, void* user_data) {
  (void)user_data;
  if (message == NULL) {
    return;
  }
  switch (message->topic) {
  case EVENT_SWEEP_STARTED:
    request_to_redraw(REDRAW_BATTERY);
    break;
  case EVENT_SWEEP_COMPLETED:
    request_to_redraw(REDRAW_PLOT | REDRAW_BATTERY);
    break;
  case EVENT_STORAGE_UPDATED:
    request_to_redraw(REDRAW_CAL_STATUS);
    break;
  default:
    break;
  }
}

void ui_attach_event_bus(event_bus_t* bus) {
  if (ui_event_bus == bus) {
    return;
  }
  ui_event_bus = bus;
  if (bus != NULL) {
    event_bus_subscribe(bus, EVENT_SWEEP_STARTED, ui_on_event, NULL);
    event_bus_subscribe(bus, EVENT_SWEEP_COMPLETED, ui_on_event, NULL);
    event_bus_subscribe(bus, EVENT_STORAGE_UPDATED, ui_on_event, NULL);
  }
}

static void ui_controller_set_request(uint8_t mask) {
  chSysLock();
  ui_request_flags |= mask;
  chSysUnlock();
}

uint8_t ui_controller_acquire_requests(uint8_t mask) { // Removed static
  chSysLock();
  uint8_t pending = ui_request_flags & mask;
  ui_request_flags &= (uint8_t)~pending;
  chSysUnlock();
  return pending;
}

uint8_t ui_controller_pending_requests(void) {
  uint8_t flags;
  chSysLock();
  flags = ui_request_flags;
  chSysUnlock();
  if (ui_board_events != NULL) {
    uint32_t pending_mask = board_events_pending_mask(ui_board_events);
    if (pending_mask & (1U << BOARD_EVENT_BUTTON)) {
      flags |= UI_CONTROLLER_REQUEST_LEVER;
    }
    if (pending_mask & (1U << BOARD_EVENT_TOUCH)) {
      flags |= UI_CONTROLLER_REQUEST_TOUCH;
    }
  }
  return flags;
}

void ui_controller_release_requests(uint8_t mask) {
  chSysLock();
  ui_request_flags &= (uint8_t)~mask;
  chSysUnlock();
}

void ui_controller_request_console_break(void) {
  ui_controller_set_request(UI_CONTROLLER_REQUEST_CONSOLE);
}

void ui_controller_dispatch_board_events(void) { // Removed static
  if (ui_board_events == NULL) {
    return;
  }
  while (board_events_dispatch(ui_board_events)) {
  }
}

static void ui_controller_on_button_event(const board_event_t* event, void* user_data) {
  (void)user_data;
  (void)event;
  ui_controller_set_request(UI_CONTROLLER_REQUEST_LEVER);
}

static void ui_controller_on_touch_event(const board_event_t* event, void* user_data) {
  (void)user_data;
  (void)event;
  ui_controller_set_request(UI_CONTROLLER_REQUEST_TOUCH);
}

void ui_controller_configure(const ui_controller_port_t* port) {
  if (port == NULL) {
    display_presenter_bind(NULL);
    ui_board_events = NULL;
    ui_board_events_subscribed = false;
    ui_attach_event_bus(NULL);
    return;
  }
  display_presenter_bind(port->display);
  ui_attach_event_bus(port->config_events);
  ui_board_events = port->board_events;
  if (ui_board_events != NULL && !ui_board_events_subscribed) {
    board_events_subscribe(ui_board_events, BOARD_EVENT_BUTTON, ui_controller_on_button_event, NULL);
    board_events_subscribe(ui_board_events, BOARD_EVENT_TOUCH, ui_controller_on_touch_event, NULL);
    ui_board_events_subscribed = true;
  }
}

void ui_controller_publish_board_event(board_event_type_t topic, uint16_t channel,
                                              bool from_isr) {
  if (ui_board_events == NULL) {
    return;
  }
  board_event_t event = {.topic = topic};
  if (topic == BOARD_EVENT_BUTTON) {
    event.data.button.channel = channel;
  } else {
    event.data.button.channel = channel;
  }
  if (from_isr) {
    board_events_publish_from_isr(ui_board_events, &event);
  } else {
    board_events_publish(ui_board_events, &event);
  }
}
