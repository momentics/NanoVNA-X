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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ui/controller/ui_controller.h"
#include "ui/ui_menu.h" // For button_t? keypads uses button_t?
#include "ui/input/hardware_input.h"

// Enums for UI mode
enum {
  UI_NORMAL,
  UI_MENU,
  UI_KEYPAD,
#ifdef __SD_FILE_BROWSER__
  UI_BROWSER,
#endif
};

// Touch Events
#define EVT_TOUCH_NONE 0
#define EVT_TOUCH_DOWN 1
#define EVT_TOUCH_PRESSED 2
#define EVT_TOUCH_RELEASED 3

// Cooperative polling budgets
#define TOUCH_RELEASE_POLL_INTERVAL_MS 2U // 500 Hz release detection
#define TOUCH_DRAG_POLL_INTERVAL_MS 8U    // 125 Hz drag updates

// File Formats (moved from ui_controller.c)
#ifdef __USE_SD_CARD__
enum {
  FMT_S1P_FILE = 0,
  FMT_S2P_FILE,
  FMT_BMP_FILE,
#ifdef __SD_CARD_DUMP_TIFF__
  FMT_TIF_FILE,
#endif
  FMT_CAL_FILE,
#ifdef __SD_CARD_DUMP_FIRMWARE__
  FMT_BIN_FILE,
#endif
#ifdef __SD_CARD_LOAD__
  FMT_CMD_FILE,
#endif
};
#endif

// Functions
void ui_init(void);
void ui_process(void);

// Attach event bus (exposed for main)
void ui_attach_event_bus(event_bus_t* bus);

// Status checks (used by menu callbacks?)
uint16_t ui_input_check(void); // Assuming this is needed
uint16_t ui_input_wait_release(void);
void ui_input_reset_state(void);

// Modes
extern uint8_t ui_mode;

// Browser mode (signature match vna_browser.c)
void ui_mode_browser(int type);

// Init and Process
void ui_init(void);
void ui_process(void);

// Mode switching
void ui_mode_normal(void);
void ui_mode_menu(void);
void ui_mode_keypad(int mode);

// Exposed handlers
void ui_normal_lever(uint16_t status);
void ui_normal_touch(int touch_x, int touch_y);

#ifdef __SD_FILE_BROWSER__
// The ui_mode_browser declaration was moved above.
#endif

// Touch functions needed for calibration
void ui_touch_cal_exec(void);
void ui_touch_draw_test(void);

// Helper for message box
void ui_message_box(const char* header, const char* text, uint32_t delay);

// Helper for checking touch release (blocking)
void touch_wait_release(void);

// Exposed touch functions
int touch_check(void);
void touch_start_watchdog(void);
void touch_stop_watchdog(void);
extern int16_t last_touch_x;
extern int16_t last_touch_y;

void touch_position(int* x, int* y);
void ui_message_box(const char* header, const char* text, uint32_t delay);
void ui_message_box_draw(const char* header, const char* text);
void ui_touch_cal_exec(void);
void ui_touch_draw_test(void);

// Draw button helper (exposed for menu engine)
void ui_draw_button(uint16_t x, uint16_t y, uint16_t w, uint16_t h, button_t* b);

// Keyboard callback type and macro
typedef void (*keyboard_cb_t)(uint16_t data, button_t* b);
#define UI_KEYBOARD_CALLBACK(ui_kb_function_name)                                                  \
  void ui_kb_function_name(uint16_t data, button_t* b)

// Needed for menu callbacks to invoke keyboard
void ui_keyboard_cb(uint16_t data, button_t* b);
void menu_sdcard_cb(uint16_t data);
void menu_stored_trace_acb(uint16_t data, button_t* b);
void menu_vna_mode_acb(uint16_t data, button_t* b);
bool select_lever_mode(int mode);
void apply_vna_mode(uint16_t idx, vna_mode_ops operation);

