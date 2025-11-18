#pragma once

#include <stdint.h>

#include "menu_controller/display_presenter.h"
#include "platform/boards/board_events.h"
#include "services/event_bus.h"

typedef struct {
  board_events_t* board_events;
  const display_presenter_t* display;
  event_bus_t* config_events;
} ui_controller_port_t;

enum {
  UI_CONTROLLER_REQUEST_NONE = 0x00,
  UI_CONTROLLER_REQUEST_LEVER = 0x01,
  UI_CONTROLLER_REQUEST_TOUCH = 0x02,
  UI_CONTROLLER_REQUEST_CONSOLE = 0x04,
};

void ui_controller_configure(const ui_controller_port_t* port);
uint8_t ui_controller_pending_requests(void);
void ui_controller_release_requests(uint8_t mask);
void ui_controller_request_console_break(void);
