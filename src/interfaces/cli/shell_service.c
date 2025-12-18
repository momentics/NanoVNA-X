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

#include "interfaces/cli/shell_service.h"

#include "ch.h"
#include "hal.h"
#ifndef NANOVNA_HOST_TEST
#include "hal_channels.h"
#endif

#include "nanovna.h"
#include "platform/peripherals/usbcfg.h"

#include <chprintf.h>
#include <stdarg.h>

static const vna_shell_command* command_table = NULL;

static BaseSequentialStream* shell_stream = NULL;
static threads_queue_t shell_thread;
static char* shell_args[VNA_SHELL_MAX_ARGUMENTS + 1];
static uint16_t shell_nargs;
static volatile const vna_shell_command* pending_command = NULL;
static uint16_t pending_argc = 0;
static char** pending_argv = NULL;
static bool shell_skip_linefeed = false;
static event_bus_t* shell_event_bus = NULL;
static shell_session_callback_t shell_session_start_cb = NULL;
static shell_session_callback_t shell_session_stop_cb = NULL;
static bool shell_session_active = false;

static void shell_on_event(const event_bus_message_t* message, void* user_data);

static void shell_assign_stream(BaseSequentialStream* stream) {
  shell_stream = stream;
}

static inline BaseAsynchronousChannel* shell_current_channel(void) {
  return (BaseAsynchronousChannel*)shell_stream;
}

#define SHELL_IO_TIMEOUT MS2ST(20)
#define SHELL_IO_CHUNK_SIZE 64U
/*
 * Deferred (mutex) commands like `scan` may take tens of seconds at low RBW / bandwidth
 * settings (and/or many points). If the wait times out, the shell thread prints a new
 * prompt while the sweep is still running, which desynchronizes host tools and makes
 * them interpret old/partial buffers as new segments.
 */
#define SHELL_DEFERRED_EXECUTION_TIMEOUT MS2ST(300000) // 5 minutes

static bool shell_io_write(const uint8_t* data, size_t size) {
  BaseAsynchronousChannel* channel = shell_current_channel();
  if (channel == NULL || data == NULL) {
    return false;
  }
  size_t written = 0;
  // Use a counter to prevent infinite blocking if the host is unresponsive
  // 100 retries * 10ms = 1000ms timeout
  int retries = 0;
  const int max_retries = 100;

  while (written < size) {
    size_t chunk = size - written;
    if (chunk > SHELL_IO_CHUNK_SIZE) {
      chunk = SHELL_IO_CHUNK_SIZE;
    }
    size_t sent = chnWriteTimeout(channel, data + written, chunk, SHELL_IO_TIMEOUT);
    if (sent == 0) {
      if (!shell_check_connect()) {
        return false;
      }
      if (++retries > max_retries) {
        return false;
      }
      chThdSleepMilliseconds(10);
      continue;
    }
    written += sent;
    retries = 0; // Reset retry counter on successful write
  }
  return true;
}

static size_t shell_io_read(uint8_t* data, size_t size) {
  BaseAsynchronousChannel* channel = shell_current_channel();
  if (channel == NULL || data == NULL) {
    return 0;
  }
  size_t received = 0;
  while (received < size) {
    size_t chunk = chnReadTimeout(channel, data + received, size - received, SHELL_IO_TIMEOUT);
    if (chunk == 0) {
      if (!shell_check_connect()) {
        break;
      }
      chThdSleepMilliseconds(5);
      continue;
    }
    received += chunk;
  }
  return received;
}

static void shell_write(const void* buf, size_t size) {
  (void)shell_io_write((const uint8_t*)buf, size);
}

static size_t shell_read(void* buf, size_t size) {
  return shell_io_read((uint8_t*)buf, size);
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

// Function to wake up all shell threads, safe to call from USB event handler
void shell_wake_all_waiting_threads(void) {
#ifdef NANOVNA_HOST_TEST
  // In test environment, we don't have full OSAL implementation
  // This is a placeholder for the real implementation
#else
  osalSysLockFromISR();
  // Wake up all waiting threads with MSG_RESET (-2) to unblock them
  osalThreadDequeueAllI(&shell_thread, (msg_t)-2);
  osalSysUnlockFromISR();
#endif
}

static void shell_handle_session_transition(bool active) {
  if (active && !shell_session_active) {
    shell_session_active = true;
    if (shell_session_start_cb != NULL) {
      shell_session_start_cb();
    }
  } else if (!active && shell_session_active) {
    shell_session_active = false;
    if (shell_session_stop_cb != NULL) {
      shell_session_stop_cb();
    }
  }
}

#ifdef __USE_SERIAL_CONSOLE__
#define PREPARE_STREAM                                                                             \
  do {                                                                                             \
    shell_assign_stream(VNA_MODE(VNA_MODE_CONNECTION) ? (BaseSequentialStream*)&SD1                \
                                                      : (BaseSequentialStream*)&SDU1);             \
  } while (false)
#else
#define PREPARE_STREAM                                                                             \
  do {                                                                                             \
    shell_assign_stream((BaseSequentialStream*)&SDU1);                                             \
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

/* Global variable to track VCP connection state */
static volatile bool vcp_connected_state = false;

/* Function to update the VCP connection state - called from USB CDC hook */
void shell_update_vcp_connection_state(bool connected) {
  vcp_connected_state = connected;
}

bool shell_check_connect(void) {
#ifdef __USE_SERIAL_CONSOLE__
  if (VNA_MODE(VNA_MODE_CONNECTION)) {
    shell_handle_session_transition(true);
    return true;
  }
  osalSysLock();
  const bool active = usb_is_active_locked();
  osalSysUnlock();
  shell_handle_session_transition(active);
  return active;
#else
#ifdef NANOVNA_HOST_TEST
  /* For host tests, just check USB state since there's no real USB stack */
  const bool active = SDU1.config->usbp->state == USB_ACTIVE;
#else
  const bool usb_active = SDU1.config->usbp->state == USB_ACTIVE;
  const bool active = usb_active && vcp_connected_state;
#endif
  shell_handle_session_transition(active);
  return active;
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

void shell_register_commands(const vna_shell_command* table) {
  command_table = table;
}

const vna_shell_command* shell_parse_command(char* line, uint16_t* argc, char*** argv,
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
  for (const vna_shell_command* cmd = command_table; cmd->sc_name != NULL; cmd++) {
    if (get_str_index(cmd->sc_name, shell_args[0]) == 0) {
      return cmd;
    }
  }
  return NULL;
}

void shell_request_deferred_execution(const vna_shell_command* command, uint16_t argc, char** argv) {
  pending_command = command;
  pending_argc = argc;
  pending_argv = argv;
  osalSysLock();
  osalThreadEnqueueTimeoutS(&shell_thread, SHELL_DEFERRED_EXECUTION_TIMEOUT);
  osalSysUnlock();
  if (shell_event_bus != NULL) {
    event_bus_publish(shell_event_bus, EVENT_USB_COMMAND_PENDING, NULL);
  }
}

void shell_service_pending_commands(void) {
  while (true) {
    osalSysLock();
    const vna_shell_command* command = (const vna_shell_command*)pending_command;
    uint16_t argc = pending_argc;
    char** argv = pending_argv;
    if (command == NULL) {
      osalSysUnlock();
      break;
    }
    pending_command = NULL;
    osalSysUnlock();

    if ((command->flags & CMD_BREAK_SWEEP) || (command->flags & CMD_WAIT_MUTEX)) {
      pause_sweep();
    }
    command->sc_function(argc, argv);


    osalSysLock();
    // In the real system, we would check if threads are waiting before dequeuing
    // For safety, we assume a thread is waiting and proceed with dequeue
    // In test environment, this is simplified
    osalThreadDequeueNextI(&shell_thread, MSG_OK);
    osalSysUnlock();
  }
}

void shell_attach_event_bus(event_bus_t* bus) {
  if (shell_event_bus == bus) {
    return;
  }
  shell_event_bus = bus;
  if (bus != NULL) {
    event_bus_subscribe(bus, EVENT_USB_COMMAND_PENDING, shell_on_event, NULL);
  }
}

void shell_register_session_start_callback(shell_session_callback_t callback) {
  shell_session_start_cb = callback;
  if (shell_session_active && shell_session_start_cb != NULL) {
    shell_session_start_cb();
  }
}

void shell_register_session_stop_callback(shell_session_callback_t callback) {
  shell_session_stop_cb = callback;
}

static void shell_on_event(const event_bus_message_t* message, void* user_data) {
  (void)user_data;
  if (message == NULL) {
    return;
  }
  if (message->topic != EVENT_USB_COMMAND_PENDING) {
    return;
  }
  shell_service_pending_commands();
}

static const char backspace[] = {0x08, 0x20, 0x08, 0x00};

int vna_shell_read_line(char* line, int max_size) {
  uint8_t c;
  uint16_t j = 0;
  while (shell_read(&c, 1)) {
    if (shell_skip_linefeed) {
      shell_skip_linefeed = false;
      if (c == '\n') {
        continue;
      }
    }
    if (c == 0x08 || c == 0x7f) {
      if (j > 0) {
        shell_write(backspace, sizeof backspace);
        j--;
      }
      continue;
    }
    if (c == '\r' || c == '\n') {
      shell_skip_linefeed = (c == '\r');
      shell_printf(VNA_SHELL_NEWLINE_STR);
      line[j] = 0;
      return 1;
    }
    if (c < ' ' || j >= max_size - 1) {
      continue;
    }
    shell_write(&c, 1);
    line[j++] = (char)c;
  }
  return 0;
}

void vna_shell_execute_cmd_line(char* line) {
  BaseSequentialStream* previous = shell_stream;
  shell_assign_stream(NULL);
  uint16_t argc = 0;
  char** argv = NULL;
  const vna_shell_command* cmd = shell_parse_command(line, &argc, &argv, NULL);
  if (cmd != NULL && (cmd->flags & CMD_RUN_IN_LOAD)) {
    cmd->sc_function(argc, argv);
  }
  shell_assign_stream(previous);
  if (shell_stream == NULL) {
    shell_restore_stream();
  }
}
