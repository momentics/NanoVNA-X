/*
 * Shell service
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

#include "app/shell.h"

#include "ch.h"
#include "hal.h"

#include "nanovna.h"
#include "usbcfg.h"

#include <chprintf.h>
#include <stdarg.h>

static const VNAShellCommand* command_table = NULL;

static BaseSequentialStream* shell_stream = NULL;
static threads_queue_t shell_thread;
static char* shell_args[VNA_SHELL_MAX_ARGUMENTS + 1];
static uint16_t shell_nargs;
static volatile const VNAShellCommand* pending_command = NULL;
static uint16_t pending_argc = 0;
static char** pending_argv = NULL;
static bool shell_skip_linefeed = false;

static void shell_write(const void* buf, size_t size) {
  if (shell_stream == NULL) {
    return;
  }
  streamWrite(shell_stream, buf, size);
}

static int shell_read(void* buf, uint32_t size) {
  if (shell_stream == NULL) {
    return 0;
  }
  return (int)chnReadTimeout((BaseAsynchronousChannel*)shell_stream, buf, size,
                             TIME_MS2I(20));
}

int shell_printf(const char* fmt, ...) {
  if (shell_stream == NULL) {
    return 0;
  }
  va_list ap;
  va_start(ap, fmt);
  const int written = chvprintf(shell_stream, fmt, ap);
  va_end(ap);
  return written;
}

#ifdef __USE_SERIAL_CONSOLE__
int serial_shell_printf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int written = chvprintf((BaseSequentialStream*)&SD1, fmt, ap);
  va_end(ap);
  return written;
}
#endif

void shell_stream_write(const void* buffer, size_t size) {
  shell_write(buffer, size);
}

#ifdef __USE_SERIAL_CONSOLE__
#define PREPARE_STREAM                                                                             \
  do {                                                                                             \
    shell_stream = VNA_MODE(VNA_MODE_CONNECTION) ? (BaseSequentialStream*)&SD1                     \
                                                 : (BaseSequentialStream*)&SDU1;                   \
  } while (false)
#else
#define PREPARE_STREAM                                                                             \
  do {                                                                                             \
    shell_stream = (BaseSequentialStream*)&SDU1;                                                   \
  } while (false)
#endif

void shell_update_speed(uint32_t speed) {
  config._serial_speed = speed;
#ifdef __USE_SERIAL_CONSOLE__
  sdSetBaudrate(&SD1, speed);
#endif
}

#ifdef __USE_SERIAL_CONSOLE__
static bool usb_is_active_locked(void) {
  return usbGetDriverStateI(&USBD1) == USB_ACTIVE;
}
#endif

void shell_reset_console(void) {
  osalSysLock();
#ifdef __USE_SERIAL_CONSOLE__
  if (usb_is_active_locked()) {
    if (VNA_MODE(VNA_MODE_CONNECTION)) {
      sduDisconnectI(&SDU1);
    } else {
      sduConfigureHookI(&SDU1);
    }
  }
  qResetI(&SD1.oqueue);
  qResetI(&SD1.iqueue);
#endif
  osalSysUnlock();
  shell_restore_stream();
}

bool shell_check_connect(void) {
#ifdef __USE_SERIAL_CONSOLE__
  if (VNA_MODE(VNA_MODE_CONNECTION)) {
    return true;
  }
  osalSysLock();
  const bool active = usb_is_active_locked();
  osalSysUnlock();
  return active;
#else
  return SDU1.config->usbp->state == USB_ACTIVE;
#endif
}

void shell_init_connection(void) {
  osalThreadQueueObjectInit(&shell_thread);
  sduObjectInit(&SDU1);
  sduStart(&SDU1, &serusbcfg);
#ifdef __USE_SERIAL_CONSOLE__
  SerialConfig serial_cfg = {config._serial_speed, 0, USART_CR2_STOP1_BITS, 0};
  sdStart(&SD1, &serial_cfg);
  shell_update_speed(config._serial_speed);
#endif
  usbDisconnectBus(&USBD1);
  chThdSleepMilliseconds(100);
  usbStart(&USBD1, &usbcfg);
  usbConnectBus(&USBD1);
  shell_restore_stream();
}

void shell_restore_stream(void) {
  PREPARE_STREAM;
}

void shell_register_commands(const VNAShellCommand* table) {
  command_table = table;
}

const VNAShellCommand* shell_parse_command(char* line, uint16_t* argc, char*** argv,
                                           const char** name_out) {
  shell_nargs = parse_line(line, shell_args, ARRAY_COUNT(shell_args));
  if (shell_nargs > ARRAY_COUNT(shell_args)) {
    shell_printf("too many arguments, max " define_to_STR(VNA_SHELL_MAX_ARGUMENTS)
                     VNA_SHELL_NEWLINE_STR);
    return NULL;
  }
  if (shell_nargs == 0) {
    if (argc) {
      *argc = 0;
    }
    if (argv) {
      *argv = NULL;
    }
    if (name_out) {
      *name_out = NULL;
    }
    return NULL;
  }
  if (argc) {
    *argc = shell_nargs - 1;
  }
  if (argv) {
    *argv = &shell_args[1];
  }
  if (name_out) {
    *name_out = shell_args[0];
  }
  if (command_table == NULL) {
    return NULL;
  }
  for (const VNAShellCommand* cmd = command_table; cmd->sc_name != NULL; cmd++) {
    if (get_str_index(cmd->sc_name, shell_args[0]) == 0) {
      return cmd;
    }
  }
  return NULL;
}

void shell_request_deferred_execution(const VNAShellCommand* command, uint16_t argc, char** argv) {
  pending_command = command;
  pending_argc = argc;
  pending_argv = argv;
  osalSysLock();
  osalThreadEnqueueTimeoutS(&shell_thread, TIME_INFINITE);
  osalSysUnlock();
}

void shell_service_pending_commands(void) {
  while (pending_command != NULL) {
    const VNAShellCommand* command = pending_command;
    command->sc_function(pending_argc, pending_argv);
    osalSysLock();
    pending_command = NULL;
    osalThreadDequeueNextI(&shell_thread, MSG_OK);
    osalSysUnlock();
  }
}

static const char backspace[] = {0x08, 0x20, 0x08, 0x00};

int vna_shell_read_line(char* line, int max_size) {
  static uint16_t current_length = 0;
  uint8_t c;
  while (shell_read(&c, 1)) {
    if (shell_skip_linefeed) {
      shell_skip_linefeed = false;
      if (c == '\n') {
        continue;
      }
    }
    if (c == 0x08 || c == 0x7f) {
      if (current_length > 0) {
        shell_write(backspace, sizeof backspace);
        current_length--;
      }
      continue;
    }
    if (c == '\r' || c == '\n') {
      shell_skip_linefeed = (c == '\r');
      shell_printf(VNA_SHELL_NEWLINE_STR);
      line[current_length] = 0;
      current_length = 0;
      return 1;
    }
    if (c < ' ' || current_length >= max_size - 1) {
      continue;
    }
    shell_write(&c, 1);
    line[current_length++] = (char)c;
  }
  return 0;
}

void vna_shell_execute_cmd_line(char* line) {
  BaseSequentialStream* previous = shell_stream;
  shell_stream = NULL;
  uint16_t argc = 0;
  char** argv = NULL;
  const VNAShellCommand* cmd = shell_parse_command(line, &argc, &argv, NULL);
  if (cmd != NULL && (cmd->flags & CMD_RUN_IN_LOAD)) {
    cmd->sc_function(argc, argv);
  }
  shell_stream = previous;
  if (shell_stream == NULL) {
    shell_restore_stream();
  }
}