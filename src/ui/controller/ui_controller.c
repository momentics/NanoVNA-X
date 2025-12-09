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

#include "nanovna.h"
#include "interfaces/cli/shell_service.h"
#include "infra/state/state_manager.h"
#include "hal.h"
#include "infra/event/event_bus.h"
#include "infra/storage/config_service.h"
#include "ui/menus/menu_main.h"
#include "ui/menus/menu_stimulus.h"
#include "ui/menus/menu_system.h"
#include "ui/menus/menu_display.h"
#include "ui/menus/menu_marker.h"
#include "ui/menus/menu_storage.h"
#include "chprintf.h"
#include "chmemcore.h"
#include <stddef.h>
#include <string.h>
#include "platform/peripherals/si5351.h"
#include "ui/input/hardware_input.h"
#include "ui/display/display_presenter.h"
#include "ui/core/ui_core.h"
#include "ui/core/ui_menu_engine.h"
#include "ui/core/ui_keypad.h"
#include "platform/boards/board_events.h"

// Use size optimization (UI not need fast speed, better have smallest size)
#pragma GCC optimize("Os")

// Touch screen


// Cooperative polling budgets for constrained UI loop. The sweep thread must
// yield within 16 ms to keep the display responsive on the STM32F303/F072
// boards (72 MHz Cortex-M4 or 48 MHz Cortex-M0+ with tight SRAM). Keep touch
// polling slices comfortably below this bound.


//==============================================
// menu_button_height removed


// Menu structs moved to UI headers

#ifdef __DFU_SOFTWARE_MODE__
void ui_enter_dfu(void) {
  touch_stop_watchdog();
  int x = 5, y = 20;
  lcd_set_colors(LCD_FG_COLOR, LCD_BG_COLOR);
  // leave a last message
  lcd_clear_screen();
  lcd_drawstring(x, y,
                 "DFU: Device Firmware Update Mode\n"
                 "To exit DFU mode, please reset device yourself.");
  boardDFUEnter();
}
#endif

bool select_lever_mode(int mode) {
  if (lever_mode == mode)
    return false;
  lever_mode = mode;
  request_to_redraw(REDRAW_BACKUP | REDRAW_FREQUENCY | REDRAW_MARKER);
  return true;
}

extern const menuitem_t menu_trace[];

extern const menuitem_t menu_marker[];






// Process keyboard button callback, and run keyboard function
// ui_keyboard_cb prototype removed (impl is below)

// Custom keyboard menu button callback (depend from current trace)








//=====================================================================================================
//                                 SD card save / load functions
//=====================================================================================================
#if STORED_TRACES > 0
UI_FUNCTION_ADV_CALLBACK(menu_stored_trace_acb) {
  if (b) {
    b->p1.text = get_stored_traces() & (1 << data) ? "CLEAR" : "STORE";
    return;
  }
  toggle_stored_trace(data);
}
#endif

//=====================================================================================================
//                                 UI menus
//=====================================================================================================



#if defined(__SD_FILE_BROWSER__)
#define MENU_STATE_SD_ENTRY 1
#else
#define MENU_STATE_SD_ENTRY 0
#endif


//====================================== end menu processing
//==========================================

//=====================================================================================================
//                                  KEYBOARD macros, variables
//=====================================================================================================


// Keyboard size and position data
// Keypad layouts moved to ui/core/ui_keypad.c

// Get value from keyboard functions
// Helpers moved to ui/core/ui_keypad.c


const keypads_list keypads_mode_tbl[KM_NONE] = {
    //                      key format     data for cb    text at bottom        callback function
    [KM_START] = {KEYPAD_FREQ, ST_START, "START", input_freq},          // start
    [KM_STOP] = {KEYPAD_FREQ, ST_STOP, "STOP", input_freq},             // stop
    [KM_CENTER] = {KEYPAD_FREQ, ST_CENTER, "CENTER", input_freq},       // center
    [KM_SPAN] = {KEYPAD_FREQ, ST_SPAN, "SPAN", input_freq},             // span
    [KM_CW] = {KEYPAD_FREQ, ST_CW, "CW FREQ", input_freq},              // cw freq
    [KM_STEP] = {KEYPAD_FREQ, ST_STEP, "FREQ STEP", input_freq},        // freq as point step
    [KM_VAR] = {KEYPAD_FREQ, ST_VAR, "JOG STEP", input_freq},           // VAR freq step
    [KM_POINTS] = {KEYPAD_UFLOAT, 0, "POINTS", input_points},           // Points num
    [KM_TOP] = {KEYPAD_MFLOAT, 0, "TOP", input_amplitude},              // top graph value
    [KM_nTOP] = {KEYPAD_NFLOAT, 0, "TOP", input_amplitude},             // top graph value
    [KM_BOTTOM] = {KEYPAD_MFLOAT, 1, "BOTTOM", input_amplitude},        // bottom graph value
    [KM_nBOTTOM] = {KEYPAD_NFLOAT, 1, "BOTTOM", input_amplitude},       // bottom graph value
    [KM_SCALE] = {KEYPAD_UFLOAT, KM_SCALE, "SCALE", input_scale},       // scale
    [KM_nSCALE] = {KEYPAD_NFLOAT, KM_nSCALE, "SCALE", input_scale},     // nano / pico scale value
    [KM_REFPOS] = {KEYPAD_FLOAT, 0, "REFPOS", input_ref},               // refpos
    [KM_EDELAY] = {KEYPAD_NFLOAT, 0, "E-DELAY", input_edelay},          // electrical delay
    [KM_VAR_DELAY] = {KEYPAD_NFLOAT, 0, "JOG STEP", input_var_delay},   // VAR electrical delay
    [KM_S21OFFSET] = {KEYPAD_FLOAT, 0, "S21 OFFSET", input_s21_offset}, // S21 level offset
    [KM_VELOCITY_FACTOR] = {KEYPAD_PERCENT, 0, "VELOCITY%%", input_velocity}, // velocity factor
#ifdef __S11_CABLE_MEASURE__
    [KM_ACTUAL_CABLE_LEN] = {KEYPAD_MKUFLOAT, 0, "CABLE LENGTH",
                             input_cable_len}, // real cable length input for VF calculation
#endif
    [KM_XTAL] = {KEYPAD_FREQ, 0, "TCXO 26M" S_Hz, input_xtal},      // XTAL frequency
    [KM_THRESHOLD] = {KEYPAD_FREQ, 0, "THRESHOLD", input_harmonic}, // Harmonic threshold frequency
    [KM_VBAT] = {KEYPAD_UFLOAT, 0, "BAT OFFSET", input_vbat},       // Vbat offset input in mV
#ifdef __S21_MEASURE__
    [KM_MEASURE_R] = {KEYPAD_UFLOAT, 0, "MEASURE Rl", input_measure_r}, // CH0 port impedance in Om
#endif
#ifdef __VNA_Z_RENORMALIZATION__
    [KM_Z_PORT] = {KEYPAD_UFLOAT, 0, "PORT Z 50" S_RARROW,
                   input_portz}, // Port Z renormalization impedance
    [KM_CAL_LOAD_R] = {KEYPAD_UFLOAT, 1, "STANDARD\n LOAD R",
                       input_portz}, // Calibration standard load R
#endif
#ifdef __USE_RTC__
    [KM_RTC_DATE] = {KEYPAD_UFLOAT, KM_RTC_DATE, "SET DATE\nYY MM DD", input_date_time}, // Date
    [KM_RTC_TIME] = {KEYPAD_UFLOAT, KM_RTC_TIME, "SET TIME\nHH MM SS", input_date_time}, // Time
    [KM_RTC_CAL] = {KEYPAD_FLOAT, 0, "RTC CAL", input_rtc_cal}, // RTC calibration in ppm
#endif
#ifdef __USE_SD_CARD__
    [KM_S1P_NAME] = {KEYPAD_TEXT, FMT_S1P_FILE, "S1P", input_filename},
    [KM_S2P_NAME] = {KEYPAD_TEXT, FMT_S2P_FILE, "S2P", input_filename},
    [KM_BMP_NAME] = {KEYPAD_TEXT, FMT_BMP_FILE, "BMP", input_filename},
#ifdef __SD_CARD_DUMP_TIFF__
    [KM_TIF_NAME] = {KEYPAD_TEXT, FMT_TIF_FILE, "TIF", input_filename},
#endif
    [KM_CAL_NAME] = {KEYPAD_TEXT, FMT_CAL_FILE, "CAL", input_filename},
#ifdef __SD_CARD_DUMP_FIRMWARE__
    [KM_BIN_NAME] = {KEYPAD_TEXT, FMT_BIN_FILE, "BIN", input_filename},
#endif
#endif
};

// Keyboard callback function for UI button
void ui_keyboard_cb(uint16_t data, button_t* b) {
  const keyboard_cb_t cb = keypads_mode_tbl[data].cb;
  if (cb)
    cb(keypads_mode_tbl[data].data, b);
}

// Keypad engine moved to ui/core/ui_keypad.c

//==================================== end keyboard input
//=============================================

//=====================================================================================================
//                                 Normal plot functions
//=====================================================================================================
void ui_mode_normal(void) {
  if (ui_mode == UI_NORMAL)
    return;

  set_area_size(AREA_WIDTH_NORMAL, AREA_HEIGHT_NORMAL);
  if (ui_mode == UI_MENU)
    request_to_draw_cells_behind_menu();
#ifdef __SD_FILE_BROWSER__
  if (ui_mode == UI_KEYPAD || ui_mode == UI_BROWSER)
    request_to_redraw(REDRAW_ALL);
#else
  if (ui_mode == UI_KEYPAD)
    request_to_redraw(REDRAW_ALL);
#endif
  ui_mode = UI_NORMAL;
}

#define MARKER_SPEEDUP 3
static void lever_move_marker(uint16_t status) {
  if (active_marker == MARKER_INVALID || !markers[active_marker].enabled)
    return;
  uint16_t step = 1 << MARKER_SPEEDUP;
  do {
    int idx = (int)markers[active_marker].index;
    if ((status & EVT_DOWN) && (idx -= step >> MARKER_SPEEDUP) < 0)
      idx = 0;
    if ((status & EVT_UP) && (idx += step >> MARKER_SPEEDUP) > sweep_points - 1)
      idx = sweep_points - 1;
    set_marker_index(active_marker, idx);
    redraw_marker(active_marker);
    step++;
  } while ((status = ui_input_wait_release()) != 0);
}

#ifdef UI_USE_LEVELER_SEARCH_MODE
static void lever_search_marker(int status) {
  if (active_marker == active_marker)
    return;
  if (status & EVT_DOWN)
    marker_search_dir(markers[active_marker].index, MK_SEARCH_LEFT);
  else if (status & EVT_UP)
    marker_search_dir(markers[active_marker].index, MK_SEARCH_RIGHT);
}
#endif

// ex. 10942 -> 10000
//      6791 ->  5000
//       341 ->   200
static freq_t step_round(freq_t v) {
  // decade step
  freq_t nx, x = 1;
  while ((nx = x * 10) < v)
    x = nx;
  // 1-2-5 step
  if (x * 2 > v)
    return x;
  if (x * 5 > v)
    return x * 2;
  return x * 5;
}

static void lever_frequency(uint16_t status) {
  uint16_t mode;
  freq_t freq;
  if (lever_mode == LM_FREQ_0) {
    if (FREQ_IS_STARTSTOP()) {
      mode = ST_START;
      freq = get_sweep_frequency(ST_START);
    } else {
      mode = ST_CENTER;
      freq = get_sweep_frequency(ST_CENTER);
    }
  } else {
    if (FREQ_IS_STARTSTOP()) {
      mode = ST_STOP;
      freq = get_sweep_frequency(ST_STOP);
    } else {
      mode = ST_SPAN;
      freq = get_sweep_frequency(ST_SPAN);
    }
  }
  if (mode == ST_SPAN && !var_freq) {
    if (status & EVT_UP)
      freq = step_round(freq * 4 + 1);
    if (status & EVT_DOWN)
      freq = step_round(freq - 1);
  } else {
    freq_t step = var_freq ? var_freq : step_round(get_sweep_frequency(ST_SPAN) / 4);
    if (status & EVT_UP)
      freq += step;
    if (status & EVT_DOWN)
      freq -= step;
  }
  while (ui_input_wait_release() != 0)
    ;
  if (freq > FREQUENCY_MAX || freq < FREQUENCY_MIN)
    return;
  set_sweep_frequency(mode, freq);
}

#define STEPRATIO 0.2f
static void lever_edelay(uint16_t status) {
  int ch = current_trace != TRACE_INVALID ? trace[current_trace].channel : 0;
  float value = current_props._electrical_delay[ch];
  if (current_props._var_delay == 0.0f) {
    float ratio = value > 0 ? STEPRATIO : -STEPRATIO;
    if (status & EVT_UP)
      value *= (1.0f + ratio);
    if (status & EVT_DOWN)
      value *= (1.0f - ratio);
  } else {
    if (status & EVT_UP)
      value += current_props._var_delay;
    if (status & EVT_DOWN)
      value -= current_props._var_delay;
  }
  set_electrical_delay(ch, value);
  while (ui_input_wait_release() != 0)
    ;
}

static bool touch_pickup_marker(int touch_x, int touch_y) {
  touch_x -= OFFSETX;
  touch_y -= OFFSETY;
  int i = MARKER_INVALID, mt, m, t;
  int min_dist = MARKER_PICKUP_DISTANCE * MARKER_PICKUP_DISTANCE;
  // Search closest marker to touch position
  for (t = 0; t < TRACES_MAX; t++) {
    if (!trace[t].enabled)
      continue;
    for (m = 0; m < MARKERS_MAX; m++) {
      if (!markers[m].enabled)
        continue;
      // Get distance to marker from touch point
      int dist = distance_to_index(t, markers[m].index, touch_x, touch_y);
      if (dist < min_dist) {
        min_dist = dist;
        i = m;
        mt = t;
      }
    }
  }
  // Marker not found
  if (i == MARKER_INVALID)
    return FALSE;
  // Marker found, set as active and start drag it
  if (active_marker != i) {
    previous_marker = active_marker;
    active_marker = i;
  }
  // Disable tracking
  props_mode &= ~TD_MARKER_TRACK;
  // Leveler mode = marker move
  select_lever_mode(LM_MARKER);
  // select trace
  set_active_trace(mt);
  // drag marker until release
  while (true) {
    int status = touch_check();
    if (status == EVT_TOUCH_RELEASED)
      break;
    if (status == EVT_TOUCH_NONE) {
      chThdSleepMilliseconds(TOUCH_DRAG_POLL_INTERVAL_MS);
      continue;
    }
    touch_position(&touch_x, &touch_y);
    int index = search_nearest_index(touch_x - OFFSETX, touch_y - OFFSETY, current_trace);
    if (index >= 0 && markers[active_marker].index != index) {
      set_marker_index(active_marker, index);
      redraw_marker(active_marker);
    }
    chThdSleepMilliseconds(TOUCH_DRAG_POLL_INTERVAL_MS);
  }
  return TRUE;
}

static bool touch_lever_mode_select(int touch_x, int touch_y) {
  int mode = -1;
  if (touch_y > HEIGHT && (props_mode & DOMAIN_MODE) == DOMAIN_FREQ) // Only for frequency domain
    mode = touch_x < FREQUENCIES_XPOS2 ? LM_FREQ_0 : LM_FREQ_1;
  if (touch_y < UI_MARKER_Y0)
    mode = (touch_x < (LCD_WIDTH / 2) && get_electrical_delay() != 0.0f) ? LM_EDELAY : LM_MARKER;
  if (mode == -1)
    return FALSE;

  touch_wait_release();
  // Check already selected
  if (select_lever_mode(mode))
    return TRUE;
  // Call keyboard for enter
  switch (mode) {
  case LM_FREQ_0:
    ui_mode_keypad(FREQ_IS_CENTERSPAN() ? KM_CENTER : KM_START);
    break;
  case LM_FREQ_1:
    ui_mode_keypad(FREQ_IS_CENTERSPAN() ? KM_SPAN : KM_STOP);
    break;
  case LM_EDELAY:
    ui_mode_keypad(KM_EDELAY);
    break;
  }
  return TRUE;
}

void ui_normal_lever(uint16_t status) {
  if (status & EVT_BUTTON_SINGLE_CLICK) {
    ui_mode_menu();
    return;
  }
  switch (lever_mode) {
  case LM_MARKER:
    lever_move_marker(status);
    break;
#ifdef UI_USE_LEVELER_SEARCH_MODE
  case LM_SEARCH:
    lever_search_marker(status);
    break;
#endif
  case LM_FREQ_0:
  case LM_FREQ_1:
    lever_frequency(status);
    break;
  case LM_EDELAY:
    lever_edelay(status);
    break;
  }
}

static bool touch_apply_ref_scale(int touch_x, int touch_y) {
  int t = current_trace;
  // do not scale invalid or smith chart
  if (t == TRACE_INVALID || trace[t].type == TRC_SMITH)
    return FALSE;
  if (touch_x < UI_SCALE_REF_X0 || touch_x > UI_SCALE_REF_X1 || touch_y < OFFSETY ||
      touch_y > AREA_HEIGHT_NORMAL)
    return FALSE;
  float ref = get_trace_refpos(t);
  float scale = get_trace_scale(t);

  if (touch_y < GRIDY * 1 * NGRIDY / 4)
    ref += 0.5f;
  else if (touch_y < GRIDY * 2 * NGRIDY / 4) {
    scale *= 2.0f;
    ref = ref / 2.0f - NGRIDY / 4 + NGRIDY / 2;
  } else if (touch_y < GRIDY * 3 * NGRIDY / 4) {
    scale /= 2.0f;
    ref = ref * 2.0f - NGRIDY + NGRIDY / 2;
  } else
    ref -= 0.5f;

  set_trace_scale(t, scale);
  set_trace_refpos(t, ref);
  chThdSleepMilliseconds(200);
  return TRUE;
}

void ui_normal_touch(int touch_x, int touch_y) {
  if (touch_pickup_marker(touch_x, touch_y))
    return; // Try drag marker
  if (touch_lever_mode_select(touch_x, touch_y))
    return; // Try select lever mode (top and bottom screen)
  if (touch_apply_ref_scale(touch_x, touch_y))
    return; // Try apply ref / scale
  // default: switch menu mode after release
  touch_wait_release();
  ui_mode_menu();
}
//================================== end normal plot input
//============================================

// Core processing moved to ui/core/ui_core.c
