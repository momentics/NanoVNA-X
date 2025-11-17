/*
 * USB/Shell subsystem port
 *
 * Centralises access to the USB shell server without altering the
 * existing behaviour of the shell implementation.
 */

#pragma once

#include <stdbool.h>

#include "app/shell.h"

typedef int (*usb_server_printf_fn_t)(const char* fmt, ...);

typedef struct {
  void (*register_commands)(const VNAShellCommand* table);
  void (*init_connection)(void);
  void (*process_pending_commands)(void);
  bool (*has_pending_io)(void);
  bool (*check_connection)(void);
  int (*read_line)(char* buffer, int max_size);
  void (*execute_cmd_line)(char* line);
  usb_server_printf_fn_t printf_fn;
} usb_server_port_api_t;

typedef struct {
  void* context;
  const usb_server_port_api_t* api;
} usb_server_port_t;

typedef struct {
  usb_server_port_t port;
} usb_server_module_t;

void usb_server_module_init(usb_server_module_t* module);
const usb_server_port_t* usb_server_module_port(usb_server_module_t* module);
