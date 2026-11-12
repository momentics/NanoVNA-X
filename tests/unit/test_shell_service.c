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
 * Host-side coverage for src/sys/shell_service.c.
 *
 * The CLI normally talks to the ChibiOS USB stack; for the host build we
 * replace the USB driver with an in-memory stream so that I/O, deferred command
 * scheduling, and event-bus integration can be verified deterministically.
 * Each test feeds a scripted RX buffer and inspects the TX buffer to ensure
 * the shell echoes characters, detects overflow conditions, and drains the
 * pending command queue when the runtime worker polls it or receives an event.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nanovna.h"
#include "driver/usbcfg.h"
#include "sys/shell_service.h"
#include "chprintf.h"
#include "memstreams.h"

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
static systime_t g_time;
static const size_t* g_write_script;
static size_t g_write_script_length;
static size_t g_write_calls;
static size_t g_disconnect_on_write;

systime_t chVTGetSystemTimeX(void) {
  return g_time;
}

/* Implementation of msObjectInit for the test stub */
void msObjectInit(MemoryStream *msp, uint8_t *buffer, size_t size, size_t eos) {
    msp->vmt = NULL; // Not used in test logic directly
    msp->buffer = buffer;
    msp->size = size;
    msp->eos = eos;
}

static int g_queue_enqueues = 0;
static int g_queue_dequeues = 0;
static int g_queue_dequeue_all_calls = 0;
static bool g_deferred_callback_running = false;
static int g_sys_lock_depth = 0;
static threads_queue_t* g_waiting_queue = NULL;
static bool g_service_before_lock = false;
static int g_prelock_service_calls = 0;
static bool g_service_via_event = false;
static event_bus_listener_t g_registered_listener = NULL;
static event_bus_topic_t g_registered_topic = EVENT_SWEEP_STARTED;
static event_bus_topic_t g_published_events[4];
static size_t g_published_event_count = 0;

static int g_failures = 0;

#define CHECK(cond, msg)                                                                         \
  do {                                                                                           \
    if (!(cond)) {                                                                               \
      ++g_failures;                                                                              \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg);                            \
    }                                                                                            \
  } while (0)

static void trigger_pending_event(void);

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
  shell_stream_state_t* state = stream_from_channel(chp);
  if (state == NULL || data == NULL) {
    return 0;
  }
  size_t space = sizeof(state->tx) - state->tx_len;
  size_t copy = size < space ? size : space;
  if (g_write_calls < g_write_script_length && copy > g_write_script[g_write_calls]) {
    copy = g_write_script[g_write_calls];
  }
  g_write_calls++;
  if (g_write_calls == g_disconnect_on_write) {
    USBD1.state = 0;
  }
  if (copy == 0) {
    g_time += timeout;
  }
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
  if ((void*)chp == (void*)&SDU1) {
    char buffer[256];
    int len = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    if (len <= 0) {
      return len;
    }
    chnWriteTimeout((BaseAsynchronousChannel*)chp, (const uint8_t*)buffer, (size_t)len,
                    TIME_IMMEDIATE);
    return len;
  } else {
    /* Assume it is a MemoryStream as used in shell_service.c */
    MemoryStream* ms = (MemoryStream*)chp;
    if (ms->buffer == NULL || ms->size <= ms->eos) {
      return 0;
    }
    size_t available = ms->size - ms->eos + 1; /* vsnprintf needs space for null */
    /* Note: ms->size in shell_service logic excludes the final null byte it reserves manually.
       but standard vsnprintf expects 'size' to INCLUDE the null byte.
       shell_service allocates 128, inits ms with 127.
       So we have buffer[127] available effectively if we are at eos=0.
       we want to write up to 127 chars. vsnprintf(..., 128).
    */
    int len = vsnprintf((char*)ms->buffer + ms->eos, available, fmt, ap);
    if (len > 0) {
        /* vsnprintf returns expected length. We must clamp to written length. */
        size_t written = (size_t)len;
        if (written >= available) {
            written = available - 1;
        }
        ms->eos += written;
        return written; // Return written chars similar to chvprintf
    }
    return len;
  }
}

void chThdSleepMilliseconds(uint32_t ms) {
  (void)ms;
}

void pause_sweep(void) {}
void resume_sweep(void) {}


void osalSysLock(void) {
  if (g_service_before_lock) {
    g_service_before_lock = false;
    ++g_prelock_service_calls;
    shell_service_pending_commands();
  }
  CHECK(g_sys_lock_depth == 0, "system lock must not be nested");
  g_sys_lock_depth = 1;
}

void osalSysUnlock(void) {
  CHECK(g_sys_lock_depth == 1, "system unlock requires the lock");
  g_sys_lock_depth = 0;
}
void osalThreadQueueObjectInit(threads_queue_t* queue) {
  (void)queue;
}
void osalThreadEnqueueTimeoutS(threads_queue_t* queue, systime_t timeout) {
  CHECK(g_sys_lock_depth == 1, "enqueue requires the system lock");
  CHECK(timeout == (systime_t)TIME_INFINITE, "completion must wait without a timeout");
  CHECK(g_waiting_queue == NULL, "only one shell caller may wait");
  ++g_queue_enqueues;
  g_waiting_queue = queue;
  /* Sleeping releases the lock only after the caller is queued. */
  osalSysUnlock();
  if (g_service_via_event) {
    trigger_pending_event();
  } else {
    shell_service_pending_commands();
  }
  CHECK(g_waiting_queue == NULL, "worker must wake the queued caller before return");
  osalSysLock();
}
msg_t osalThreadDequeueNextI(threads_queue_t* queue, msg_t msg) {
  CHECK(g_sys_lock_depth == 1, "completion wake requires the system lock");
  CHECK(g_waiting_queue == queue, "completion must not wake an empty queue");
  CHECK(!g_deferred_callback_running, "caller must not resume while the callback is running");
  CHECK(msg == MSG_OK, "completion wake must report success");
  g_waiting_queue = NULL;
  ++g_queue_dequeues;
  return msg;
}

void osalThreadDequeueAllI(threads_queue_t* queue, msg_t msg) {
  (void)msg;
  CHECK(g_sys_lock_depth == 1, "reset wake requires the system lock");
  ++g_queue_dequeue_all_calls;
  if (g_waiting_queue == queue) {
    g_waiting_queue = NULL;
    ++g_queue_dequeues;
  }
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

static void reset_shell_state(const char* scripted_rx) {
  memset(&g_stream_state, 0, sizeof(g_stream_state));
  g_time = 0;
  g_write_script = NULL;
  g_write_script_length = 0;
  g_write_calls = 0;
  g_disconnect_on_write = 0;
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
  g_queue_dequeue_all_calls = 0;
  g_deferred_callback_running = false;
  CHECK(g_sys_lock_depth == 0, "test must leave the system unlocked");
  CHECK(g_waiting_queue == NULL, "test must leave no waiting caller");
  g_service_before_lock = false;
  g_prelock_service_calls = 0;
  g_service_via_event = false;
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
  CHECK(g_sys_lock_depth == 0, "deferred callback must run outside the system lock");
  CHECK(g_waiting_queue != NULL, "callback must not run before the caller is queued");
  ++g_command_invocations;
  g_last_command_argc = argc;
  if (argc > 0 && argv != NULL && argv[0] != NULL) {
    strncpy(g_last_command_arg0, argv[0], sizeof(g_last_command_arg0) - 1);
  }
}

static const vna_shell_command g_test_commands[] = {
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

  const vna_shell_command* cmd = shell_parse_command(line, &argc, &argv, &name);
  CHECK(cmd == &g_test_commands[0], "registered command must be returned");
  CHECK(argc == 2, "argc should exclude the command token");
  CHECK(argv != NULL && strcmp(argv[0], "123") == 0, "argv[0] should match first parameter");
  CHECK(argv != NULL && strcmp(argv[1], "456") == 0, "argv[1] should match second parameter");
  CHECK(name != NULL && strcmp(name, "scan") == 0, "name_out should report command name");

  char overflow_line[] = "scan 1 2 3 4 5";
  uint16_t overflow_argc = 0;
  char** overflow_argv = NULL;
  const vna_shell_command* overflow_cmd =
      shell_parse_command(overflow_line, &overflow_argc, &overflow_argv, NULL);
  CHECK(overflow_cmd == &g_test_commands[0], "known commands should still parse when clamped");
  CHECK(overflow_argc == VNA_SHELL_MAX_ARGUMENTS,
        "argc must be clamped to the configured maximum");
  CHECK(strcmp(overflow_argv[VNA_SHELL_MAX_ARGUMENTS - 1], "4") == 0,
        "excess arguments should be dropped");
}

static void test_shell_deferred_queue_and_event_bus(void) {
  reset_shell_state(NULL);
  g_command_invocations = 0;
  shell_register_commands(g_test_commands);
  event_bus_t bus = {0};
  shell_attach_event_bus(&bus);
  CHECK(g_registered_listener != NULL, "attach should register event listener");
  CHECK(g_registered_topic == EVENT_USB_COMMAND_PENDING, "listener must target pending event");

  char line[] = "scan 42";
  uint16_t argc = 0;
  char** argv = NULL;
  const vna_shell_command* cmd = shell_parse_command(line, &argc, &argv, NULL);
  CHECK(cmd != NULL, "command must parse");

  g_service_via_event = true;
  shell_request_deferred_execution(cmd, argc, argv);
  CHECK(g_queue_enqueues == 1, "request should enqueue the shell caller");
  CHECK(g_published_event_count == 0, "request must not publish a late pending event");
  CHECK(g_command_invocations == 1, "callback must complete before the request returns");
  CHECK(g_queue_dequeues == 1, "worker must wake the caller before the request returns");

  trigger_pending_event();
  CHECK(g_command_invocations == 1, "late event must not execute the command again");
  CHECK(g_last_command_argc == 1, "command must receive original argc");
  CHECK(strcmp(g_last_command_arg0, "42") == 0, "command should receive argument contents");
  CHECK(g_queue_dequeues == 1, "queue dequeue should mirror execution");

  shell_service_pending_commands();
  CHECK(g_command_invocations == 1, "no second execution when queue already drained");
  shell_attach_event_bus(NULL);
}

static void test_shell_deferred_publication_is_atomic(void) {
  reset_shell_state(NULL);
  g_command_invocations = 0;
  g_last_command_argc = 0;
  memset(g_last_command_arg0, 0, sizeof(g_last_command_arg0));
  shell_attach_event_bus(NULL);
  char* argv[] = {"borrowed"};

  /* Preempt just before locking, then run the worker again as enqueue sleeps. */
  g_service_before_lock = true;
  shell_request_deferred_execution(&g_test_commands[0], 1, argv);

  CHECK(g_prelock_service_calls == 1, "test must exercise the pre-lock worker window");
  CHECK(g_queue_enqueues == 1 && g_queue_dequeues == 1, "request must complete one handshake");
  CHECK(g_command_invocations == 1, "worker must execute the command exactly once");
  CHECK(g_last_command_argc == 1, "worker must receive the published argument count");
  CHECK(strcmp(g_last_command_arg0, "borrowed") == 0, "worker must receive the borrowed argument");
  CHECK(g_sys_lock_depth == 0, "request must return with the system unlocked");
  CHECK(g_published_event_count == 0, "polling must complete without an event bus");
  shell_service_pending_commands();
  CHECK(g_command_invocations == 1 && g_queue_dequeues == 1, "completed command must be cleared");
}

static void disconnect_during_deferred_callback(int argc, char* argv[]) {
  test_command_callback(argc, argv);
  g_deferred_callback_running = true;
  threads_queue_t* waiting_queue = g_waiting_queue;
  CHECK(waiting_queue != NULL, "caller must already be waiting while the handler executes");

  /* Inject the USB reset interrupt while the handler still borrows argv. */
  USBD1.state = 0;
  osalSysLock();
  shell_wake_all_waiting_threadsI();
  osalSysUnlock();

  CHECK(g_queue_dequeue_all_calls == 0, "disconnect must not dequeue the completion queue");
  CHECK(g_waiting_queue == waiting_queue, "caller must remain blocked during the handler");
  CHECK(g_queue_dequeues == 0, "disconnect must not count as command completion");
  CHECK(argc == 1 && strcmp(argv[0], "borrowed") == 0,
        "handler must retain its borrowed arguments after disconnect");
  g_deferred_callback_running = false;
}

static void test_shell_disconnect_waits_for_deferred_completion(void) {
  reset_shell_state(NULL);
  g_command_invocations = 0;
  const vna_shell_command command = {
      .sc_name = "disconnect", .sc_function = disconnect_during_deferred_callback, .flags = 0};
  char argument[] = "borrowed";
  char* argv[] = {argument};

  shell_request_deferred_execution(&command, 1, argv);

  CHECK(!shell_check_connect(), "test must disconnect while the callback executes");
  CHECK(g_command_invocations == 1, "handler must finish before the caller returns");
  CHECK(g_queue_enqueues == 1 && g_queue_dequeues == 1,
        "only actual handler completion may release the caller");
  CHECK(g_waiting_queue == NULL && !g_deferred_callback_running,
        "completed request must leave no waiting caller or running callback");
  shell_service_pending_commands();
  CHECK(g_command_invocations == 1 && g_queue_dequeues == 1,
        "completed command must not execute or dequeue again");
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

static void test_write_inactivity_timeout(void) {
  /* A finite script makes an unbounded-retry regression fail without hanging. */
  static const size_t stalled_writes[11] = {0};
  reset_shell_state(NULL);
  g_write_script = stalled_writes;
  g_write_script_length = ARRAY_COUNT(stalled_writes);
  CHECK(shell_printf("stalled") == 0, "stalled output should report failure");
  CHECK(g_write_calls == 10 && g_time == MS2ST(1000),
        "connected stalled output should stop after ten 100ms waits");
  CHECK(g_stream_state.tx_len == 0, "stalled output should not emit data");
  CHECK(shell_check_connect(), "inactivity should not disconnect the shell session");

  g_write_script_length = 0;
  CHECK(shell_printf("recovered") == 9, "later output should succeed when the host resumes reading");
  CHECK(tx_contains("recovered"), "recovered output should reach the host");

  reset_shell_state(NULL);
  g_time = UINT32_MAX - MS2ST(500);
  systime_t start = g_time;
  g_write_script = stalled_writes;
  g_write_script_length = ARRAY_COUNT(stalled_writes);
  CHECK(shell_printf("wrap") == 0, "stalled formatted output should fail across clock wraparound");
  CHECK(g_write_calls == 10 && (systime_t)(g_time - start) == MS2ST(1000),
        "inactivity timeout should survive system clock wraparound");
  CHECK(g_stream_state.tx_len == 0, "stalled formatted output should not emit data across wraparound");
}

static void test_raw_write_backpressure(void) {
  static const size_t partial_writes[] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  reset_shell_state(NULL);
  g_write_script = partial_writes;
  g_write_script_length = ARRAY_COUNT(partial_writes);
  shell_stream_write("progress", 8);
  CHECK(g_stream_state.tx_len == 8 && memcmp(g_stream_state.tx, "progress", 8) == 0,
        "raw output must preserve every byte despite more than one second without progress");
   CHECK(g_time == MS2ST(1200) && g_write_calls == ARRAY_COUNT(partial_writes) + 1,
        "raw output must keep retrying until the host resumes reading");
  shell_stream_write("next", 4);
  CHECK(g_stream_state.tx_len == 12 && memcmp(g_stream_state.tx, "progressnext", 12) == 0,
        "subsequent raw chunks must follow the complete stalled chunk without missing bytes");
}

static void test_write_partial_progress(void) {
  static const size_t partial_writes[] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
  };
  reset_shell_state(NULL);
  g_write_script = partial_writes;
  g_write_script_length = ARRAY_COUNT(partial_writes);
  CHECK(shell_printf("progress") == 8, "progressing output should complete after partial writes");
  CHECK(g_stream_state.tx_len == 8 && memcmp(g_stream_state.tx, "progress", 8) == 0,
        "partial writes should preserve every byte across intermittent stalls");
  CHECK(g_time == MS2ST(2700) && g_write_calls == ARRAY_COUNT(partial_writes) + 1,
        "each positive write should renew the timeout beyond one second total");
}

static void test_write_disconnected(void) {
  static const size_t partial_writes[] = {2, 0};
  reset_shell_state(NULL);
  g_write_script = partial_writes;
  g_write_script_length = ARRAY_COUNT(partial_writes);
  g_disconnect_on_write = 2;
  CHECK(shell_printf("disconnect") == 0, "disconnect during output should report failure");
  CHECK(g_write_calls == 2 && g_time == MS2ST(100),
        "disconnect during a stalled write should return before the inactivity deadline");
  CHECK(g_stream_state.tx_len == 2 && memcmp(g_stream_state.tx, "di", 2) == 0,
        "disconnect should preserve only bytes already transmitted");
  CHECK(!shell_check_connect(), "USB disconnect should end the shell session");

  reset_shell_state(NULL);
  USBD1.state = 0;
  g_write_script = partial_writes + 1;
  g_write_script_length = 1;
  CHECK(shell_printf("offline") == 0, "already disconnected output should report failure");
  CHECK(g_write_calls == 1 && g_time == MS2ST(100) && g_stream_state.tx_len == 0,
        "already disconnected output should stop at the first failed write");
}

int main(void) {
  test_write_inactivity_timeout();
  test_raw_write_backpressure();
  test_write_partial_progress();
  test_write_disconnected();
  test_shell_parse_and_overflow();
  test_shell_deferred_queue_and_event_bus();
  test_shell_deferred_publication_is_atomic();
  test_shell_disconnect_waits_for_deferred_completion();
  test_shell_read_line_and_echo();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_shell_service");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
