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

/**
 * Host-side regression tests for src/core/common.c helpers.
 *
 * These helpers implement the NanoVNA console parsing routines and are heavily
 * relied upon by both the CLI and SD-card subsystems.  Because they operate on
 * plain C strings, we can execute them on any POSIX host and catch subtle bugs
 * (locale differences, integer overflows, parsing regressions) before the code
 * ever reaches the STM32 firmware.  The assertions below are intentionally
 * verbose so CI logs tell a clear story when a regression appears.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Production symbols implemented in src/core/common.c */
extern int32_t my_atoi(const char* p);
extern uint32_t my_atoui(const char* p);
extern float my_atof(const char* p);
extern int get_str_index(const char* value, const char* list);
extern bool strcmpi(const char* lhs, const char* rhs);
extern int parse_line(char* line, char* args[], int max_cnt);
extern void swap_bytes(uint16_t* buf, int size);
extern int packbits(char* source, char* dest, int size);

static int g_failures = 0;

#define CHECK_WITH_MSG(cond, fmt, ...)                                                        \
  do {                                                                                        \
    if (!(cond)) {                                                                            \
      ++g_failures;                                                                           \
      fprintf(stderr, "[FAIL] %s:%d: " fmt "\n", __FILE__, __LINE__, __VA_ARGS__);             \
    }                                                                                         \
  } while (0)

#define CHECK(cond) CHECK_WITH_MSG(cond, "condition '%s' failed", #cond)

static void test_my_atoi(void) {
  /*
   * my_atoi needs to mimic stdlib atoi but without locale baggage.  These
   * checks cover signed, unsigned, trimmed and early-terminating inputs so
   * parser regressions show up immediately.
   */
  CHECK(my_atoi("0") == 0);
  CHECK(my_atoi("+17") == 17);
  CHECK(my_atoi("-2048") == -2048);
  CHECK(my_atoi("123abc") == 123); /* parser stops at the first non-digit */
  CHECK(my_atoi("-0") == 0);
}

static void test_my_atoui(void) {
  /*
   * Unsigned parsing accepts decimal by default and binary/octal/hex when the
   * input starts with 0b/0o/0x.  Each path is validated explicitly.
   */
  CHECK(my_atoui("15") == 15U);
  CHECK(my_atoui("0x10") == 16U);
  CHECK(my_atoui("0o77") == 63U);
  CHECK(my_atoui("0b1011") == 11U);
  CHECK(my_atoui("+42") == 42U);
}

static void test_my_atof(void) {
  /*
   * The floating-point parser supports decimal separators "." and "," along
   * with engineering suffixes (G/M/k/m/u/n/p).  Each case captures a realistic
   * CLI command the firmware must parse in the field.
   */
  CHECK(fabsf(my_atof("3.14") - 3.14f) < 1e-6f);
  CHECK(fabsf(my_atof("2,5") - 2.5f) < 1e-6f); /* comma as decimal separator */
  CHECK(fabsf(my_atof("1.5k") - 1500.0f) < 1e-3f);
  CHECK(fabsf(my_atof("-10m") + 0.01f) < 1e-6f); /* milli suffix */
  CHECK(fabsf(my_atof("5u") - 5e-6f) < 1e-12f);
  CHECK(fabsf(my_atof("6.02E3") - 6020.0f) < 1e-3f);
}

static void test_strcmpi(void) {
  /* Case-insensitive comparison must treat ASCII letters equally. */
  CHECK(strcmpi("abc", "ABC"));
  CHECK(strcmpi("NanoVNA", "nanovna"));
  CHECK(!strcmpi("foo", "bar"));
  CHECK(strcmpi("", ""));
}

static void test_get_str_index(void) {
  /*
   * Parameter parsing relies on pipe-separated option lists.  The helper should
   * return the zero-based index of the match or -1 when not found.
   */
  CHECK(get_str_index("center", "start|stop|center|span|cw") == 2);
  CHECK(get_str_index("span", "start|stop|center|span|cw") == 3);
  CHECK(get_str_index("nope", "start|stop") == -1);
}

static void test_parse_line(void) {
  /*
   * parse_line() is the shell's workhorse.  Validate quoted segments,
   * whitespace folding, and argv bounding to ensure the CLI does not regress.
   */
  char buffer[] = "scan 10 \"quoted arg\" tail";
  char* argv[4];
  int argc = parse_line(buffer, argv, 4);
  CHECK(argc == 4);
  CHECK(strcmp(argv[0], "scan") == 0);
  CHECK(strcmp(argv[1], "10") == 0);
  CHECK(strcmp(argv[2], "quoted arg") == 0);
  CHECK(strcmp(argv[3], "tail") == 0);

  char small_buf[] = "a b c d";
  char* small_argv[2];
  int small_argc = parse_line(small_buf, small_argv, 2);
  CHECK(small_argc == 2);
  CHECK(strcmp(small_argv[0], "a") == 0);
  CHECK(strcmp(small_argv[1], "b") == 0);
}

static void test_swap_bytes(void) {
  /*
   * swap_bytes() is hot in the USB dump path.  Guard endian swapping logic so
   * future optimisations cannot silently corrupt data streams.
   */
  uint16_t data[] = {0x1234, 0xABCD, 0x00FF};
  swap_bytes(data, (int)(sizeof(data) / sizeof(data[0])));
  CHECK(data[0] == 0x3412);
  CHECK(data[1] == 0xCDAB);
  CHECK(data[2] == 0xFF00);
}

static void unpack_packbits(const char* packed, size_t packed_len, char* out, size_t* out_len) {
  size_t out_index = 0;
  for (size_t i = 0; i < packed_len;) {
    int8_t header = packed[i++];
    if (header >= 0) {
      size_t count = (size_t)header + 1U;
      memcpy(&out[out_index], &packed[i], count);
      i += count;
      out_index += count;
    } else {
      size_t count = (size_t)(-header) + 1U;
      char value = packed[i++];
      for (size_t j = 0; j < count; ++j) {
        out[out_index++] = value;
      }
    }
  }
  *out_len = out_index;
}

static void test_packbits_roundtrip(void) {
  /*
   * PackBits compression is used for SD screenshots.  A round-trip guarantees
   * we continue to emit streams that a compliant decoder can digest.
   */
  const char payload[] = "AAAABBBCCXYZDDDDEEEFAAAABBBB"; /* mixes literals and runs */
  char compressed[64];
  int packed = packbits((char*)payload, compressed, (int)strlen(payload));
  CHECK(packed > 0);
  char restored[sizeof(payload)];
  size_t restored_len = 0;
  unpack_packbits(compressed, (size_t)packed, restored, &restored_len);
  CHECK(restored_len == strlen(payload));
  CHECK(memcmp(restored, payload, strlen(payload)) == 0);
}

int main(void) {
  test_my_atoi();
  test_my_atoui();
  test_my_atof();
  test_strcmpi();
  test_get_str_index();
  test_parse_line();
  test_swap_bytes();
  test_packbits_roundtrip();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_common");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
