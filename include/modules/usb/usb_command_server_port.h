/*
 * USB command server transport port definition.
 *
 * Provides a transport-agnostic interface that the CLI server can use
 * to exchange data with the host and react to session lifecycle events.
 */

#pragma once

#include "app/shell.h"
#include "services/event_bus.h"

typedef void (*usb_command_server_session_cb_t)(void);

typedef struct usb_command_server_port usb_command_server_port_t;

typedef struct usb_command_server_port_api {
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
  void (*attach_event_bus)(event_bus_t* bus);
  void (*on_session_start)(usb_command_server_session_cb_t callback);
  void (*on_session_stop)(usb_command_server_session_cb_t callback);
} usb_command_server_port_api_t;

struct usb_command_server_port {
  void* context;
  const usb_command_server_port_api_t* api;
};

extern const usb_command_server_port_api_t usb_command_server_port_api;

