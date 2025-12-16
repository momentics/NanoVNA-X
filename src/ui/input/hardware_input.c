/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
 * Based on TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
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

#include "ui/input/hardware_input.h"

static uint16_t last_button = 0;
static systime_t last_button_down_ticks;
static systime_t last_button_repeat_ticks;

static uint16_t read_buttons(void) {
  return palReadPort(GPIOA) & (BUTTON_DOWN | BUTTON_PUSH | BUTTON_UP);
}

void ui_input_reset_state(void) {
  last_button = read_buttons();
  last_button_down_ticks = chVTGetSystemTimeX();
  last_button_repeat_ticks = last_button_down_ticks;
}

uint16_t ui_input_get_buttons(void) {
  uint16_t cur_button = read_buttons();
#ifdef __FLIP_DISPLAY__
  if (VNA_MODE(VNA_MODE_FLIP_DISPLAY) &&
      (((cur_button >> GPIOA_LEVER1) ^ (cur_button >> GPIOA_LEVER2)) & 1)) {
    cur_button ^= (1 << GPIOA_LEVER1) | (1 << GPIOA_LEVER2);
  }
#endif
  return cur_button;
}

uint16_t ui_input_check(void) {
  systime_t ticks;
  while (true) {
    ticks = chVTGetSystemTimeX();
    if (ticks - last_button_down_ticks > BUTTON_DEBOUNCE_TICKS) {
      break;
    }
    chThdSleepMilliseconds(2);
  }
  uint16_t status = NO_EVENT;
  uint16_t cur_button = ui_input_get_buttons();
  uint16_t button_set = (last_button ^ cur_button) & cur_button;
  last_button_down_ticks = ticks;
  last_button = cur_button;

  if (button_set & BUTTON_PUSH) {
    status |= EVT_BUTTON_SINGLE_CLICK;
  }
  if (button_set & BUTTON_UP) {
    status |= EVT_UP;
  }
  if (button_set & BUTTON_DOWN) {
    status |= EVT_DOWN;
  }
  return status;
}

uint16_t ui_input_wait_release(void) {
  while (true) {
    systime_t ticks = chVTGetSystemTimeX();
    systime_t dt = ticks - last_button_down_ticks;
    chThdSleepMilliseconds(10);
    uint16_t cur_button = ui_input_get_buttons();
    uint16_t changed = last_button ^ cur_button;
    if (dt >= BUTTON_DOWN_LONG_TICKS && (cur_button & BUTTON_PUSH)) {
      return EVT_BUTTON_DOWN_LONG;
    }
    if (changed & BUTTON_PUSH) {
      return EVT_BUTTON_SINGLE_CLICK;
    }
    if (changed) {
      last_button = cur_button;
      last_button_down_ticks = ticks;
      return NO_EVENT;
    }
    if (dt > BUTTON_DOWN_LONG_TICKS && ticks > last_button_repeat_ticks) {
      uint16_t status = NO_EVENT;
      if (cur_button & BUTTON_DOWN) {
        status |= EVT_DOWN | EVT_REPEAT;
      }
      if (cur_button & BUTTON_UP) {
        status |= EVT_UP | EVT_REPEAT;
      }
      last_button_repeat_ticks = ticks + BUTTON_REPEAT_TICKS;
      return status;
    }
  }
}
