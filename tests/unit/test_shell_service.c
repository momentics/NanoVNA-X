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

/*
 * Host-side coverage for src/interfaces/cli/shell_service.c.
 *
+ * The CLI normally talks to the ChibiOS USB stack; for the host build we
 * replace the USB driver with an in-memory stream so that I/O, deferred command
 * scheduling, and event-bus integration can be verified deterministically.
 * Each test feeds a scripted RX buffer and inspects the TX buffer to ensure
 * the shell echoes characters, detects overflow conditions, and drains the
 * pending command queue whenever EVENT_USB_COMMAND_PENDING fires.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nanovna.h"
#include "platform/peripherals/usbcfg.h"
#include "interfaces/cli/shell_service.h"
#include "chprintf.h"

/* ------------------------------------------------------------------------- */
/*                  Minimal USB/stream plumbing for the tests                */

typedef struct {
  uint8_t rx[256];
  size_t rx_len;
  size_t rx_pos;
  uint8_t tx[512];
  size_t tx_len;
} shell_stream_state_t;

config_t config = {._serial_speed = 115200};
USBConfig usbcfg = {0};
SerialUSBConfig serusbcfg = {0};
USBDriver USBD1 = {.state = USB_ACTIVE};
SerialUSBDriver SDU1 = {.stream = {.vmt = NULL}, .config = &serusbcfg, .user_data = NULL};

static shell_stream_state_t g_stream_state;
static int g_queue_enqueues = 0;
static int g_queue_dequeues = 0;
static event_bus_listener_t g_registered_listener = NULL;
static event_bus_topic_t g_registered_topic = EVENT_SWEEP_STARTED;
static event_bus_topic_t g_published_events[4];
static size_t g_published_event_count = 0;

static bool tx_contains(const char* needle) {
  size_t needle_len = strlen(needle);
  if (needle_len == 0 || g_stream_state.tx_len < needle_len) {
    return false;
  }
  for (size_t i = 0; i <= g_stream_state.tx_len - needle_len; ++i) {
    if (memcmp(&g_stream_state.tx[i], needle, needle_len) == 0) {
      return true;
    }
  }
  return false;
}

static shell_stream_state_t* stream_from_channel(BaseAsynchronousChannel* chp) {
  SerialUSBDriver* drv = (SerialUSBDriver*)chp;
  return (drv != NULL) ? (shell_stream_state_t*)drv->user_data : NULL;
}

size_t chnWriteTimeout(BaseAsynchronousChannel* chp, const uint8_t* data, size_t size,
                       systime_t timeout) {
  (void)timeout;
  shell_stream_state_t* state = stream_from_channel(chp);
  if (state == NULL || data == NULL) {
    return 0;
  }
  size_t space = sizeof(state->tx) - state->tx_len;
  size_t copy = size < space ? size : space;
  memcpy(&state->tx[state->tx_len], data, copy);
  state->tx_len += copy;
  return copy;
}

size_t chnReadTimeout(BaseAsynchronousChannel* chp, uint8_t* data, size_t size,
                      systime_t timeout) {
  (void)timeout;
  shell_stream_state_t* state = stream_from_channel(chp);
  if (state == NULL || data == NULL) {
    return 0;
  }
  if (state->rx_pos >= state->rx_len) {
    return 0;
  }
  size_t remaining = state->rx_len - state->rx_pos;
  size_t copy = remaining < size ? remaining : size;
  memcpy(data, &state->rx[state->rx_pos], copy);
  state->rx_pos += copy;
  return copy;
}

int chvprintf(BaseSequentialStream* chp, const char* fmt, va_list ap) {
  char buffer[256];
  int len = vsnprintf(buffer, sizeof(buffer), fmt, ap);
  if (len <= 0) {
    return len;
  }
  chnWriteTimeout((BaseAsynchronousChannel*)chp, (const uint8_t*)buffer, (size_t)len,
                  TIME_IMMEDIATE);
  return len;
}

void chThdSleepMilliseconds(uint32_t ms) {
  (void)ms;
}

void osalSysLock(void) {}
void osalSysUnlock(void) {}
void osalThreadQueueObjectInit(threads_queue_t* queue) {
  (void)queue;
}
void osalThreadEnqueueTimeoutS(threads_queue_t* queue, systime_t timeout) {
  (void)queue;
  (void)timeout;
  ++g_queue_enqueues;
}
msg_t osalThreadDequeueNextI(threads_queue_t* queue, msg_t msg) {
  (void)queue;
  ++g_queue_dequeues;
  return msg;
}

void sduObjectInit(SerialUSBDriver* driver) {
  (void)driver;
}
void sduStart(SerialUSBDriver* driver, const SerialUSBConfig* cfg) {
  (void)driver;
  (void)cfg;
}
void sduDisconnectI(SerialUSBDriver* driver) {
  (void)driver;
}
void sduConfigureHookI(SerialUSBDriver* driver) {
  (void)driver;
}
void usbDisconnectBus(USBDriver* driver) {
  (void)driver;
}
void usbStart(USBDriver* driver, const USBConfig* cfg) {
  (void)driver;
  (void)cfg;
}
void usbConnectBus(USBDriver* driver) {
  (void)driver;
}

/* ------------------------------------------------------------------------- */
/*                          Event bus stub helpers                           */

bool event_bus_publish(event_bus_t* bus, event_bus_topic_t topic, const void* payload) {
  (void)bus;
  (void)payload;
  if (g_published_event_count < sizeof(g_published_events) / sizeof(g_published_events[0])) {
    g_published_events[g_published_event_count++] = topic;
  }
  return true;
}

bool event_bus_subscribe(event_bus_t* bus, event_bus_topic_t topic, event_bus_listener_t listener,
                         void* user_data) {
  (void)bus;
  (void)user_data;
  g_registered_topic = topic;
  g_registered_listener = listener;
  return true;
}

/* ------------------------------------------------------------------------- */

static int g_failures = 0;

#define CHECK(cond, msg)                                                                         \
  do {                                                                                           \
    if (!(cond)) {                                                                               \
      ++g_failures;                                                                              \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg);                            \
    }                                                                                            \
  } while (0)

static void reset_shell_state(const char* scripted_rx) {
  memset(&g_stream_state, 0, sizeof(g_stream_state));
  if (scripted_rx != NULL) {
    g_stream_state.rx_len = strlen(scripted_rx);
    memcpy(g_stream_state.rx, scripted_rx, g_stream_state.rx_len);
  }
  serusbcfg.usbp = &USBD1;
  SDU1.config = &serusbcfg;
  SDU1.user_data = &g_stream_state;
  USBD1.state = USB_ACTIVE;
  config._vna_mode = 0;
  config._serial_speed = 115200;
  g_queue_enqueues = 0;
  g_queue_dequeues = 0;
  g_published_event_count = 0;
  g_registered_listener = NULL;
  shell_restore_stream();
}

/* ------------------------------------------------------------------------- */
/*                               Test helpers                                */

static int g_command_invocations = 0;
static int g_last_command_argc = 0;
static char g_last_command_arg0[32];

static void test_command_callback(int argc, char* argv[]) {
  ++g_command_invocations;
  g_last_command_argc = argc;
  if (argc > 0 && argv != NULL && argv[0] != NULL) {
    strncpy(g_last_command_arg0, argv[0], sizeof(g_last_command_arg0) - 1);
  }
}

static const VNAShellCommand g_test_commands[] = {
    {.sc_name = "scan", .sc_function = test_command_callback, .flags = 0},
    {.sc_name = NULL, .sc_function = NULL, .flags = 0},
};

static void trigger_pending_event(void) {
  if (g_registered_listener == NULL) {
    return;
  }
  event_bus_message_t message = {.topic = EVENT_USB_COMMAND_PENDING, .payload = NULL};
  g_registered_listener(&message, NULL);
}

/* ------------------------------------------------------------------------- */
/*                                  Tests                                    */

static void test_shell_parse_and_overflow(void) {
  reset_shell_state(NULL);
  shell_register_commands(g_test_commands);
  char line[] = "scan 123 456";
  uint16_t argc = 0;
  char** argv = NULL;
  const char* name = NULL;

  const VNAShellCommand* cmd = shell_parse_command(line, &argc, &argv, &name);
  CHECK(cmd == &g_test_commands[0], "registered command must be returned");
  CHECK(argc == 2, "argc should exclude the command token");
  CHECK(argv != NULL && strcmp(argv[0], "123") == 0, "argv[0] should match first parameter");
  CHECK(argv != NULL && strcmp(argv[1], "456") == 0, "argv[1] should match second parameter");
  CHECK(name != NULL && strcmp(name, "scan") == 0, "name_out should report command name");

  char overflow_line[] = "scan 1 2 3 4 5";
  uint16_t overflow_argc = 0;
  char** overflow_argv = NULL;
  const VNAShellCommand* overflow_cmd =
      shell_parse_command(overflow_line, &overflow_argc, &overflow_argv, NULL);
  CHECK(overflow_cmd == &g_test_commands[0], "known commands should still parse when clamped");
  CHECK(overflow_argc == VNA_SHELL_MAX_ARGUMENTS,
        "argc must be clamped to the configured maximum");
  CHECK(strcmp(overflow_argv[VNA_SHELL_MAX_ARGUMENTS - 1], "4") == 0,
        "excess arguments should be dropped");
}

static void test_shell_deferred_queue_and_event_bus(void) {
  reset_shell_state(NULL);
  shell_register_commands(g_test_commands);
  event_bus_t bus = {0};
  shell_attach_event_bus(&bus);
  CHECK(g_registered_listener != NULL, "attach should register event listener");
  CHECK(g_registered_topic == EVENT_USB_COMMAND_PENDING, "listener must target pending event");

  char line[] = "scan 42";
  uint16_t argc = 0;
  char** argv = NULL;
  const VNAShellCommand* cmd = shell_parse_command(line, &argc, &argv, NULL);
  CHECK(cmd != NULL, "command must parse");

  shell_request_deferred_execution(cmd, argc, argv);
  CHECK(g_queue_enqueues == 1, "request should enqueue a worker wakeup");
  CHECK(g_published_event_count == 1, "pending event must be published");
  CHECK(g_published_events[0] == EVENT_USB_COMMAND_PENDING,
        "pending event topic must match specification");

  trigger_pending_event();
  CHECK(g_command_invocations == 1, "event callback should drain pending command");
  CHECK(g_last_command_argc == 1, "command must receive original argc");
  CHECK(strcmp(g_last_command_arg0, "42") == 0, "command should receive argument contents");
  CHECK(g_queue_dequeues == 1, "queue dequeue should mirror execution");

  shell_service_pending_commands();
  CHECK(g_command_invocations == 1, "no second execution when queue already drained");
}

static void test_shell_read_line_and_echo(void) {
  reset_shell_state("he\x7Flo\r\n");
  char line[32];
  int status = vna_shell_read_line(line, sizeof(line));
  CHECK(status == 1, "read_line should complete on CR/LF");
  CHECK(strcmp(line, "hlo") == 0, "backspace must remove the previous character");
  CHECK(g_stream_state.tx_len > 0, "shell should echo characters to the TX buffer");
  CHECK(tx_contains(VNA_SHELL_NEWLINE_STR), "entering a line should emit a newline");
}

int main(void) {
  test_shell_parse_and_overflow();
  test_shell_deferred_queue_and_event_bus();
  test_shell_read_line_and_echo();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_shell_service");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
