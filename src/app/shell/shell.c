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

#define SHELL_READ_TIMEOUT MS2ST(50)
#define SHELL_INPUT_IDLE_TIMEOUT MS2ST(2000)
#define SHELL_WRITE_TIMEOUT MS2ST(200)
#define SHELL_WRITE_CHUNK 64U

#define SHELL_PRINTF_MAX_FILLER 11
#define SHELL_PRINTF_FLOAT_PRECISION 9

static char* shell_long_to_string_with_divisor(char* p, long num, unsigned radix, long divisor) {
  int i;
  char* q;
  long l = num;
  long ll = (divisor == 0) ? num : divisor;

  q = p + SHELL_PRINTF_MAX_FILLER;
  do {
    i = (int)(l % radix);
    i += '0';
    if (i > '9') {
      i += 'A' - '0' - 10;
    }
    *--q = (char)i;
    l /= radix;
  } while ((ll /= radix) != 0);

  i = (int)(p + SHELL_PRINTF_MAX_FILLER - q);
  do {
    *p++ = *q++;
  } while (--i);

  return p;
}

static char* shell_ltoa(char* p, long num, unsigned radix) {
  return shell_long_to_string_with_divisor(p, num, radix, 0);
}

#if CHPRINTF_USE_FLOAT
static const long shell_pow10[SHELL_PRINTF_FLOAT_PRECISION] = {
    10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

static char* shell_ftoa(char* p, double num, unsigned long precision) {
  long l;

  if (precision == 0 || precision > SHELL_PRINTF_FLOAT_PRECISION) {
    precision = SHELL_PRINTF_FLOAT_PRECISION;
  }
  precision = shell_pow10[precision - 1];

  l = (long)num;
  p = shell_long_to_string_with_divisor(p, l, 10, 0);
  *p++ = '.';
  l = (long)((num - l) * precision);
  return shell_long_to_string_with_divisor(p, l, 10, precision / 10);
}
#endif

static bool shell_stream_put_char(BaseSequentialStream* stream, uint8_t value);
static size_t shell_stream_write_buffer(BaseSequentialStream* stream, const void* buf,
                                        size_t size);

static void shell_write(const void* buf, size_t size) {
  if (shell_stream == NULL || size == 0U) {
    return;
  }
  (void)shell_stream_write_buffer(shell_stream, buf, size);
}

static size_t shell_read(void* buf, size_t size) {
  if (shell_stream == NULL || size == 0) {
    return 0;
  }

#ifdef __USE_SERIAL_CONSOLE__
  if (shell_stream == (BaseSequentialStream*)&SD1) {
    return iqReadTimeout(&SD1.iqueue, (uint8_t*)buf, size, SHELL_READ_TIMEOUT);
  }
#endif

#if HAL_USE_SERIAL_USB == TRUE
  if (shell_stream == (BaseSequentialStream*)&SDU1) {
    return ibqReadTimeout(&SDU1.ibqueue, (uint8_t*)buf, size, SHELL_READ_TIMEOUT);
  }
#endif

  return streamRead(shell_stream, buf, size);
}

static size_t shell_stream_write_buffer(BaseSequentialStream* stream, const void* buf,
                                        size_t size) {
  if (stream == NULL || size == 0U) {
    return 0U;
  }

  const uint8_t* data = (const uint8_t*)buf;
  size_t total = 0U;

#if HAL_USE_SERIAL_USB == TRUE
  if (stream == (BaseSequentialStream*)&SDU1) {
    while (total < size) {
      size_t chunk = size - total;
      if (chunk > SHELL_WRITE_CHUNK) {
        chunk = SHELL_WRITE_CHUNK;
      }

      const syssts_t sts = chSysGetStatusAndLockX();
      const bool active =
          (usbGetDriverStateI(&USBD1) == USB_ACTIVE) && (SDU1.state == SDU_READY);
      chSysRestoreStatusX(sts);
      if (!active) {
        break;
      }

      const size_t written =
          obqWriteTimeout(&SDU1.obqueue, data + total, chunk, SHELL_WRITE_TIMEOUT);
      if (written == 0U) {
        break;
      }

      total += written;
      if (total < size) {
        chThdYield();
      }
    }

    if (total > 0U) {
      obqFlush(&SDU1.obqueue);
    }

    return total;
  }
#endif

#ifdef __USE_SERIAL_CONSOLE__
  if (stream == (BaseSequentialStream*)&SD1) {
    while (total < size) {
      size_t chunk = size - total;
      if (chunk > SHELL_WRITE_CHUNK) {
        chunk = SHELL_WRITE_CHUNK;
      }

      const qsize_t written =
          oqWriteTimeout(&SD1.oqueue, data + total, (qsize_t)chunk, SHELL_WRITE_TIMEOUT);
      if (written == 0U) {
        break;
      }

      total += (size_t)written;
      if (total < size) {
        chThdYield();
      }
    }

    return total;
  }
#endif

  return streamWrite(stream, data, size);
}

static bool shell_stream_put_char(BaseSequentialStream* stream, uint8_t value) {
  return shell_stream_write_buffer(stream, &value, 1U) == 1U;
}

static int shell_vprintf(BaseSequentialStream* stream, const char* fmt, va_list ap) {
  if (stream == NULL) {
    return 0;
  }

  char* p;
  char* s;
  char c;
  char filler;
  int i;
  int precision;
  int width;
  int n = 0;
  bool is_long;
  bool left_align;
  long l;
#if CHPRINTF_USE_FLOAT
  float f;
  char tmpbuf[2 * SHELL_PRINTF_MAX_FILLER + 1];
#else
  char tmpbuf[SHELL_PRINTF_MAX_FILLER + 1];
#endif

  while (true) {
    c = *fmt++;
    if (c == 0) {
      return n;
    }
    if (c != '%') {
      if (!shell_stream_put_char(stream, (uint8_t)c)) {
        return n;
      }
      n++;
      continue;
    }
    p = tmpbuf;
    s = tmpbuf;
    left_align = FALSE;
    if (*fmt == '-') {
      fmt++;
      left_align = TRUE;
    }
    filler = ' ';
    if (*fmt == '0') {
      fmt++;
      filler = '0';
    }
    width = 0;
    while (TRUE) {
      c = *fmt++;
      if (c >= '0' && c <= '9') {
        c -= '0';
      } else if (c == '*') {
        c = va_arg(ap, int);
      } else {
        break;
      }
      width = width * 10 + c;
    }
    precision = 0;
    if (c == '.') {
      while (TRUE) {
        c = *fmt++;
        if (c >= '0' && c <= '9') {
          c -= '0';
        } else if (c == '*') {
          c = va_arg(ap, int);
        } else {
          break;
        }
        precision *= 10;
        precision += c;
      }
    }

    if (c == 'l' || c == 'L') {
      is_long = TRUE;
      if (*fmt) {
        c = *fmt++;
      }
    } else {
      is_long = (c >= 'A') && (c <= 'Z');
    }

    switch (c) {
    case 'c':
      filler = ' ';
      *p++ = va_arg(ap, int);
      break;
    case 's':
      filler = ' ';
      if ((s = va_arg(ap, char*)) == 0) {
        s = "(null)";
      }
      if (precision == 0) {
        precision = 32767;
      }
      while (*s && (precision-- > 0)) {
        *p++ = *s++;
      }
      break;
    case 'D':
    case 'd':
      if (is_long) {
        l = va_arg(ap, long);
      } else {
        l = va_arg(ap, int);
      }
      if (l < 0) {
        *p++ = '-';
        l = -l;
      }
      p = shell_ltoa(p, l, 10);
      break;
    case 'U':
    case 'u':
      if (is_long) {
        l = va_arg(ap, long);
      } else {
        l = va_arg(ap, int);
      }
      p = shell_ltoa(p, l, 10);
      break;
    case 'X':
    case 'x':
      if (is_long) {
        l = va_arg(ap, long);
      } else {
        l = va_arg(ap, int);
      }
      p = shell_ltoa(p, l, 16);
      break;
    case 'O':
    case 'o':
      if (is_long) {
        l = va_arg(ap, long);
      } else {
        l = va_arg(ap, int);
      }
      p = shell_ltoa(p, l, 8);
      break;
#if CHPRINTF_USE_FLOAT
    case 'f':
    case 'F':
      f = (float)va_arg(ap, double);
      if (f < 0) {
        *p++ = '-';
        f = -f;
      }
      p = shell_ftoa(p, f, precision);
      break;
#endif
    default:
      if (!shell_stream_put_char(stream, (uint8_t)c)) {
        return n;
      }
      n++;
      continue;
    }

    i = (int)(p - s);
    if ((width -= i) < 0) {
      width = 0;
    }
    if (!left_align) {
      while (width-- > 0) {
        if (!shell_stream_put_char(stream, (uint8_t)filler)) {
          return n;
        }
        n++;
      }
    }
    while (i-- > 0) {
      if (!shell_stream_put_char(stream, (uint8_t)*s++)) {
        return n;
      }
      n++;
    }
    while (width-- > 0) {
      if (!shell_stream_put_char(stream, (uint8_t)filler)) {
        return n;
      }
      n++;
    }
  }
}

int shell_printf(const char* fmt, ...) {
  if (shell_stream == NULL) {
    return 0;
  }
  va_list ap;
  va_start(ap, fmt);
  const int written = shell_vprintf(shell_stream, fmt, ap);
  va_end(ap);
  return written;
}

#ifdef __USE_SERIAL_CONSOLE__
int serial_shell_printf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int written = shell_vprintf((BaseSequentialStream*)&SD1, fmt, ap);
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
  static systime_t last_activity = 0;
  uint8_t c;
  while (true) {
    const size_t read = shell_read(&c, 1);
    if (read == 0) {
      if (current_length > 0 && last_activity != 0 &&
          chVTTimeElapsedSinceX(last_activity) >= SHELL_INPUT_IDLE_TIMEOUT) {
        shell_printf(VNA_SHELL_NEWLINE_STR "[shell] command timeout" VNA_SHELL_NEWLINE_STR);
        current_length = 0;
        last_activity = 0;
        shell_skip_linefeed = false;
        return VNA_SHELL_LINE_ABORTED;
      }
      return VNA_SHELL_LINE_IDLE;
    }

    last_activity = chVTGetSystemTimeX();

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
      last_activity = 0;
      return VNA_SHELL_LINE_READY;
    }
    if (c < ' ' || current_length >= max_size - 1) {
      continue;
    }
    shell_write(&c, 1);
    line[current_length++] = (char)c;
  }
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