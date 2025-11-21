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

#include "interfaces/ports/usb_command_server_port.h"

const usb_command_server_port_api_t usb_command_server_port_api = {
    .register_commands = shell_register_commands,
    .printf = shell_printf,
    .stream_write = shell_stream_write,
    .update_speed = shell_update_speed,
    .check_connect = shell_check_connect,
    .init_connection = shell_init_connection,
    .parse_command = shell_parse_command,
    .request_deferred_execution = shell_request_deferred_execution,
    .service_pending_commands = shell_service_pending_commands,
    .read_line = vna_shell_read_line,
    .execute_cmd_line = vna_shell_execute_cmd_line,
    .attach_event_bus = shell_attach_event_bus,
    .on_session_start = shell_register_session_start_callback,
    .on_session_stop = shell_register_session_stop_callback};
