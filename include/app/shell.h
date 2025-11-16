/*
 * Shell service interface
 *
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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "services/event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VNA_SHELL_NEWLINE_STR "\r\n"
#define VNA_SHELL_PROMPT_STR "ch> "
#define VNA_SHELL_MAX_ARGUMENTS 4
#define VNA_SHELL_MAX_LENGTH 64

typedef void (*vna_shellcmd_t)(int argc, char* argv[]);

typedef struct {
  const char* sc_name;
  vna_shellcmd_t sc_function;
  uint16_t flags;
} VNAShellCommand;

enum {
  CMD_WAIT_MUTEX = 1,
  CMD_BREAK_SWEEP = 2,
  CMD_RUN_IN_UI = 4,
  CMD_RUN_IN_LOAD = 8,
};

void shell_register_commands(const VNAShellCommand* table);

int shell_printf(const char* fmt, ...);
#ifdef __USE_SERIAL_CONSOLE__
int serial_shell_printf(const char* fmt, ...);
#endif

void shell_stream_write(const void* buffer, size_t size);
void shell_write_text(const char* text);
void shell_write_line(const char* text);

void shell_update_speed(uint32_t speed);
void shell_reset_console(void);
bool shell_check_connect(void);
void shell_init_connection(void);
void shell_restore_stream(void);
bool shell_stream_ready(void);
bool shell_try_restore_stream(void);
void shell_drop_stream(void);

const VNAShellCommand* shell_parse_command(char* line, uint16_t* argc, char*** argv,
                                           const char** name_out);
void shell_request_deferred_execution(const VNAShellCommand* command, uint16_t argc, char** argv);
void shell_service_pending_commands(void);
void shell_attach_event_bus(event_bus_t* bus);

int vna_shell_read_line(char* line, int max_size);
void vna_shell_execute_cmd_line(char* line);

#ifdef __cplusplus
}
#endif
