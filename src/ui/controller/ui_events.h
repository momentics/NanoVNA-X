#pragma once

#include "infra/event/event_bus.h"
#include "platform/boards/board_events.h"
#include "ui/controller/ui_controller.h" // For ui_controller_port_t

// UI Event Management
void ui_attach_event_bus(event_bus_t* bus);
void ui_controller_configure(const ui_controller_port_t* port);
void ui_controller_request_console_break(void);
uint8_t ui_controller_pending_requests(void);
uint8_t ui_controller_acquire_requests(uint8_t mask);
void ui_controller_release_requests(uint8_t mask);

// Internal dispatch (called by main loop)
// Expose if main loop is outside or if ui_process needs it
// ui_controller.c main loop calls it?
// ui_controller.c seems to have ui_process?
// I will check ui_process location. 
// Assuming it is in ui_controller.c and needs to call dispatch.
// But ui_controller_dispatch_board_events was static.
// So ui_process must be in ui_controller.c and called it.
// So I must expose it.
void ui_controller_dispatch_board_events(void);

// Publish event (internal use or cross-module)
void ui_controller_publish_board_event(board_event_type_t topic, uint16_t channel, bool from_isr);
