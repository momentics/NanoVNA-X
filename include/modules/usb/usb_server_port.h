/*
 * USB server (shell) subsystem port definition.
 */

#pragma once

#include "app/shell.h"


typedef struct usb_server_port usb_server_port_t;

typedef struct usb_server_port_api {
  void (*register_commands)(const VNAShellCommand* table);
  int (*printf)(const char* fmt, ...);
  void (*stream_write)(const void* buffer, size_t size);
  void (*update_speed)(uint32_t speed);
  bool (*check_connect)(void);
  void (*init_connection)(void);
  const VNAShellCommand* (*parse_command)(char* line, uint16_t* argc, char*** argv,
                                          const char** name_out);
  void (*request_deferred_execution)(const VNAShellCommand* command, uint16_t argc, char** argv);
  void (*service_pending_commands)(void);
  int (*read_line)(char* line, int max_size);
  void (*execute_cmd_line)(char* line);
} usb_server_port_api_t;

struct usb_server_port {
  void* context;
  const usb_server_port_api_t* api;
};

extern const usb_server_port_api_t usb_port_api;

