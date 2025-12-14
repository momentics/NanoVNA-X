/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include "interfaces/cli/shell_service.h"
#include "infra/event/event_bus.h"

typedef void (*usb_command_server_session_cb_t)(void);

typedef struct usb_command_server_port usb_command_server_port_t;

typedef struct usb_command_server_port_api {
  void (*register_commands)(const vna_shell_command_t *table);
  int (*printf)(const char *fmt, ...);
  void (*stream_write)(const void *buffer, size_t size);
  void (*update_speed)(uint32_t speed);
  bool (*check_connect)(void);
  void (*init_connection)(void);
  const vna_shell_command_t *(*parse_command)(char *line, uint16_t *argc, char ***argv,
                                              const char **name_out);
  void (*request_deferred_execution)(const vna_shell_command_t *command, uint16_t argc,
                                     char **argv);
  void (*service_pending_commands)(void);
  int (*read_line)(char *line, int max_size);
  void (*execute_cmd_line)(char *line);
  void (*attach_event_bus)(event_bus_t *bus);
  void (*on_session_start)(usb_command_server_session_cb_t callback);
  void (*on_session_stop)(usb_command_server_session_cb_t callback);
} usb_command_server_port_api_t;

struct usb_command_server_port {
  void *context;
  const usb_command_server_port_api_t *api;
};

extern const usb_command_server_port_api_t USB_COMMAND_SERVER_PORT_API;
