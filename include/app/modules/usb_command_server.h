/*
 * USB command server: hosts the CDC endpoint and dispatches CLI requests.
 */

#pragma once

#include "app/shell.h"
#include "services/event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*usb_command_handler_t)(char* line);

typedef struct usb_command_server {
  const VNAShellCommand* commands;
  event_bus_t* bus;
  usb_command_handler_t handler;
} usb_command_server_t;

void usb_command_server_init(usb_command_server_t* server,
                             const VNAShellCommand* command_table,
                             event_bus_t* bus,
                             usb_command_handler_t handler);
void usb_command_server_start(usb_command_server_t* server);
void usb_command_server_service(usb_command_server_t* server);
void usb_command_server_dispatch_line(usb_command_server_t* server, char* line);

#ifdef __cplusplus
}
#endif
