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





#ifndef __UI_CORE_UI_KEYPAD_H__
#define __UI_CORE_UI_KEYPAD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "ui/core/ui_core.h"

// Keypad Enumerations
enum {
  KM_START = 0,
  KM_STOP,
  KM_CENTER,
  KM_SPAN,
  KM_CW,
  KM_STEP,
  KM_VAR, // frequency input
  KM_POINTS,
  KM_TOP,
  KM_nTOP,
  KM_BOTTOM,
  KM_nBOTTOM,
  KM_SCALE,
  KM_nSCALE,
  KM_REFPOS,
  KM_EDELAY,
  KM_VAR_DELAY,
  KM_S21OFFSET,
  KM_VELOCITY_FACTOR,
#ifdef __S11_CABLE_MEASURE__
  KM_ACTUAL_CABLE_LEN,
#endif
  KM_XTAL,
  KM_THRESHOLD,
  KM_VBAT,
#ifdef __S21_MEASURE__
  KM_MEASURE_R,
#endif
#ifdef __VNA_Z_RENORMALIZATION__
  KM_Z_PORT,
  KM_CAL_LOAD_R,
#endif
#ifdef __USE_RTC__
  KM_RTC_DATE,
  KM_RTC_TIME,
  KM_RTC_CAL,
#endif
#ifdef __USE_SD_CARD__
  KM_S1P_NAME,
  KM_S2P_NAME,
  KM_BMP_NAME,
#ifdef __SD_CARD_DUMP_TIFF__
  KM_TIF_NAME,
#endif
  KM_CAL_NAME,
#ifdef __SD_CARD_DUMP_FIRMWARE__
  KM_BIN_NAME,
#endif
#endif
  KM_NONE
};

enum {
  KEYPAD_FREQ,
  KEYPAD_UFLOAT,
  KEYPAD_PERCENT,
  KEYPAD_FLOAT,
  KEYPAD_MFLOAT,
  KEYPAD_MKUFLOAT,
  KEYPAD_NFLOAT,
  KEYPAD_TEXT
};

typedef struct {
  uint8_t keypad_type;
  uint8_t data;
  const char* name;
  const keyboard_cb_t cb;
} __attribute__((packed)) keypads_list;

// Buffer for input
// Max keyboard input length
#define NUMINPUT_LEN 12
#define TXTINPUT_LEN (8)

#if NUMINPUT_LEN + 2 > TXTINPUT_LEN + 1
extern char kp_buf[NUMINPUT_LEN + 2];
#else
extern char kp_buf[TXTINPUT_LEN + 1];
#endif

// Helpers
float keyboard_get_float(void);
freq_t keyboard_get_freq(void);
uint32_t keyboard_get_uint(void);
int32_t keyboard_get_int(void);

// Handlers
void ui_keypad_lever(uint16_t status);
void ui_keypad_touch(int touch_x, int touch_y);

// Callback
void ui_keyboard_cb(uint16_t data, button_t* b);

extern uint8_t keypad_mode;

#ifdef __cplusplus
}
#endif

#endif // __UI_CORE_UI_KEYPAD_H__
