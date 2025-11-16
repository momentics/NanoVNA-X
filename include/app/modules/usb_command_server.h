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

typedef struct {
  void* context;
  const VNAShellCommand* command_table;
  event_bus_t* event_bus;
  usb_command_handler_t handler;
  bool (*check_connect)(void* context);
  int (*read_line)(void* context, char* buffer, int length);
  void (*write_prompt)(void* context, const char* prompt);
  void (*write_banner)(void* context, const char* banner);
  void (*on_session_start)(void* context);
  void (*on_session_end)(void* context);
} usb_command_server_config_t;

typedef struct usb_command_server {
  usb_command_server_config_t config;
} usb_command_server_t;

void usb_command_server_init(usb_command_server_t* server, const usb_command_server_config_t* config);
void usb_command_server_start(usb_command_server_t* server);
void usb_command_server_service(usb_command_server_t* server);
void usb_command_server_dispatch_line(usb_command_server_t* server, char* line);

#ifdef __cplusplus
}
#endif
