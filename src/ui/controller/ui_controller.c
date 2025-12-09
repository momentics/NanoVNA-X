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
#include "chprintf.h"
#include "chmemcore.h"
#include <stddef.h>
#include <string.h>
#include "platform/peripherals/si5351.h"
#include "ui/input/hardware_input.h"
#include "ui/display/display_presenter.h"
#include "ui/controller/ui_controller.h"
#include "ui/ui_internal.h"
#include "ui/input/ui_touch.h"
#include "ui/input/ui_keypad.h"
#include "ui/controller/ui_events.h"
#include "ui/menus/menu_internal.h"
#include "platform/boards/board_events.h"
#include "ui/controller/vna_browser.h"

// Use size optimization (UI not need fast speed, better have smallest size)
#pragma GCC optimize("Os")

// Touch screen
// Touch events moved to ui_touch.h

// Cooperative polling budgets for constrained UI loop. The sweep thread must
// yield within 16 ms to keep the display responsive on the STM32F303/F072
// boards (72 MHz Cortex-M4 or 48 MHz Cortex-M0+ with tight SRAM). Keep touch
// polling slices comfortably below this bound.
#define TOUCH_RELEASE_POLL_INTERVAL_MS 2U // 500 Hz release detection
#define TOUCH_DRAG_POLL_INTERVAL_MS 8U    // 125 Hz drag updates


#ifdef lcd_drawstring
#undef lcd_drawstring
#endif
#ifdef lcd_drawstring_size
#undef lcd_drawstring_size
#endif
#ifdef lcd_printf
#undef lcd_printf
#endif
#ifdef lcd_drawchar
#undef lcd_drawchar
#endif
#ifdef lcd_drawchar_size
#undef lcd_drawchar_size
#endif
#ifdef lcd_drawfont
#undef lcd_drawfont
#endif
#ifdef lcd_fill
#undef lcd_fill
#endif
#ifdef lcd_bulk
#undef lcd_bulk
#endif
#ifdef lcd_read_memory
#undef lcd_read_memory
#endif
#ifdef lcd_line
#undef lcd_line
#endif
#ifdef lcd_set_background
#undef lcd_set_background
#endif
#ifdef lcd_set_colors
#undef lcd_set_colors
#endif
#ifdef lcd_set_flip
#undef lcd_set_flip
#endif
#ifdef lcd_set_font
#undef lcd_set_font
#endif
#ifdef lcd_blit_bitmap
#undef lcd_blit_bitmap
#endif

#define lcd_drawstring(...) display_presenter_drawstring(__VA_ARGS__)
#define lcd_drawstring_size(...) display_presenter_drawstring_size(__VA_ARGS__)
#define lcd_printf(...) display_presenter_printf(__VA_ARGS__)
#define lcd_drawchar(...) display_presenter_drawchar(__VA_ARGS__)
#define lcd_drawchar_size(...) display_presenter_drawchar_size(__VA_ARGS__)
#define lcd_drawfont(...) display_presenter_drawfont(__VA_ARGS__)
#define lcd_fill(...) display_presenter_fill(__VA_ARGS__)
#define lcd_bulk(...) display_presenter_bulk(__VA_ARGS__)
#define lcd_read_memory(...) display_presenter_read_memory(__VA_ARGS__)
#define lcd_line(...) display_presenter_line(__VA_ARGS__)
#define lcd_set_background(...) display_presenter_set_background(__VA_ARGS__)
#define lcd_set_colors(...) display_presenter_set_colors(__VA_ARGS__)
#define lcd_set_flip(...) display_presenter_set_flip(__VA_ARGS__)
#define lcd_set_font(...) display_presenter_set_font(__VA_ARGS__)
#define lcd_blit_bitmap(...) display_presenter_blit_bitmap(__VA_ARGS__)



//==============================================


uint8_t keyboard_temp; // Used for SD card keyboard workflows

#ifdef __USE_SD_CARD__

#endif



uint8_t ui_mode = UI_NORMAL;

int8_t selection;

extern const menuitem_t menu_clear[];
UI_FUNCTION_ADV_CALLBACK(menu_band_sel_acb);

static uint8_t menu_current_level;
static uint8_t menu_button_height;

menuitem_t* ui_menu_list(const menu_descriptor_t* descriptors, size_t count,
                                const char* label, const void* reference,
                                menuitem_t* out) {
  if (descriptors == NULL || out == NULL)
    return out;
  for (size_t i = 0; i < count; i++) {
    out->type = descriptors[i].type;
    out->data = descriptors[i].data;
    out->label = label;
    out->reference = reference;
    out++;
  }
  return out;
}

void menu_set_next(menuitem_t* entry, const menuitem_t* next) {
  if (entry == NULL)
    return;
  entry->type = MT_NEXT;
  entry->data = 0;
  entry->label = NULL;
  entry->reference = next;
}

void ui_cycle_option(uint16_t* dst, const option_desc_t* list, size_t count, button_t* b) {
  if (dst == NULL || list == NULL || count == 0)
    return;
  size_t idx = 0;
  while (idx < count && list[idx].value != *dst)
    idx++;
  if (idx >= count)
    idx = 0;
  const option_desc_t* desc = &list[idx];
  if (b) {
    if (desc->label)
      b->p1.text = desc->label;
    if (desc->icon >= 0)
      b->icon = desc->icon;
    return;
  }
  idx = (idx + 1) % count;
  *dst = list[idx].value;
}

void ui_mode_normal(void);
static void ui_mode_menu(void);



static void menu_draw(uint32_t mask);
void menu_move_back(bool leave_ui);
void menu_push_submenu(const menuitem_t* submenu);

const menuitem_t* menu_build_save_menu(void);
const menuitem_t* menu_build_recall_menu(void);
const menuitem_t* menu_build_bandwidth_menu(void);
const menuitem_t* menu_build_points_menu(void);
const menuitem_t* menu_build_marker_select_menu(void);


const menuitem_t* menu_build_power_menu(void);
#ifdef __USE_SMOOTH__
const menuitem_t* menu_build_smooth_menu(void);
#endif

// Icons for UI
#include "ui/resources/icons/icons_menu.h"

#if 0
static void btn_wait(void) {
  while (READ_PORT()) chThdSleepMilliseconds(10);
}
#endif

#if 0
static void bubble_sort(uint16_t *v, int n) {
  bool swapped = true;
  int i = 0, j;
  while (i < n - 1 && swapped) { // keep going while we swap in the unordered part
    swapped = false;
    for (j = n - 1; j > i; j--) { // unordered part
      if (v[j] < v[j - 1]) {
        SWAP(uint16_t, v[j], v[j - 1]);
        swapped = true;
      }
    }
    i++;
  }
}
#endif

// Touch module moved to ui/input/ui_touch.c

//*******************************************************************************
//                           UI functions
//*******************************************************************************


// Draw button function
void ui_draw_button(uint16_t x, uint16_t y, uint16_t w, uint16_t h, button_t* b) {
  uint16_t type = b->border;
  uint16_t bw = type & BUTTON_BORDER_WIDTH_MASK;
  // Draw border if width > 0
  if (bw) {
    uint16_t br = LCD_RISE_EDGE_COLOR;
    uint16_t bd = LCD_FALLEN_EDGE_COLOR;
    lcd_set_background(type & BUTTON_BORDER_TOP ? br : bd);
    lcd_fill(x, y, w, bw); // top
    lcd_set_background(type & BUTTON_BORDER_LEFT ? br : bd);
    lcd_fill(x, y, bw, h); // left
    lcd_set_background(type & BUTTON_BORDER_RIGHT ? br : bd);
    lcd_fill(x + w - bw, y, bw, h); // right
    lcd_set_background(type & BUTTON_BORDER_BOTTOM ? br : bd);
    lcd_fill(x, y + h - bw, w, bw); // bottom
  }
  // Set colors for button and text
  lcd_set_colors(b->fg, b->bg);
  if (type & BUTTON_BORDER_NO_FILL)
    return;
  lcd_fill(x + bw, y + bw, w - (bw * 2), h - (bw * 2));
}

static void ui_message_box_draw(const char* header, const char* text) {
  button_t b;
  int x, y;
  b.bg = LCD_MENU_COLOR;
  b.fg = LCD_MENU_TEXT_COLOR;
  b.border = BUTTON_BORDER_FLAT;
  if (header) { // Draw header
    ui_draw_button((LCD_WIDTH - MESSAGE_BOX_WIDTH) / 2, LCD_HEIGHT / 2 - 40, MESSAGE_BOX_WIDTH, 60,
                   &b);
    x = (LCD_WIDTH - MESSAGE_BOX_WIDTH) / 2 + 10;
    y = LCD_HEIGHT / 2 - 40 + 5;
    lcd_drawstring(x, y, header);
    request_to_redraw(REDRAW_AREA);
  }
  if (text) { // Draw window
    lcd_set_colors(LCD_MENU_TEXT_COLOR, LCD_FG_COLOR);
    lcd_fill((LCD_WIDTH - MESSAGE_BOX_WIDTH) / 2 + 3, LCD_HEIGHT / 2 - 40 + FONT_STR_HEIGHT + 8,
             MESSAGE_BOX_WIDTH - 6, 60 - FONT_STR_HEIGHT - 8 - 3);
    x = (LCD_WIDTH - MESSAGE_BOX_WIDTH) / 2 + 20;
    y = LCD_HEIGHT / 2 - 40 + FONT_STR_HEIGHT + 8 + 14;
    lcd_drawstring(x, y, text);
    request_to_redraw(REDRAW_AREA);
  }
}

// Draw message box function
void ui_message_box(const char* header, const char* text, uint32_t delay) {
  ui_message_box_draw(header, text);

  do {
    chThdSleepMilliseconds(delay == 0 ? 50 : delay);
  } while (delay == 0 && ui_input_check() != EVT_BUTTON_SINGLE_CLICK &&
           touch_check() != EVT_TOUCH_PRESSED);
}



bool select_lever_mode(int mode) {
  if (lever_mode == mode)
    return false;
  lever_mode = mode;
  request_to_redraw(REDRAW_BACKUP | REDRAW_FREQUENCY | REDRAW_MARKER);
  return true;
}










#define F_S11 0x00
#define F_S21 0x80




static const option_desc_t transform_window_options[] = {
    {TD_WINDOW_MINIMUM, "MINIMUM", BUTTON_ICON_NONE},
    {TD_WINDOW_NORMAL, "NORMAL", BUTTON_ICON_NONE},
    {TD_WINDOW_MAXIMUM, "MAXIMUM", BUTTON_ICON_NONE},
};

UI_FUNCTION_ADV_CALLBACK(menu_transform_window_acb) {
  (void)data;  // Suppress unused parameter warning
  uint16_t window = props_mode & TD_WINDOW;
  ui_cycle_option(&window, transform_window_options, ARRAY_COUNT(transform_window_options), b);
  if (b)
    return;
  props_mode = (props_mode & (uint16_t)~TD_WINDOW) | window;
}

static const option_desc_t transform_state_options[] = {
    {0, "OFF", BUTTON_ICON_NOCHECK},
    {DOMAIN_TIME, "ON", BUTTON_ICON_CHECK},
};

UI_FUNCTION_ADV_CALLBACK(menu_transform_acb) {
  (void)data;
  uint16_t state = props_mode & DOMAIN_TIME;
  ui_cycle_option(&state, transform_state_options, ARRAY_COUNT(transform_state_options), b);
  if (b)
    return;
  props_mode = (props_mode & (uint16_t)~DOMAIN_TIME) | state;
  select_lever_mode(LM_MARKER);
  request_to_redraw(REDRAW_FREQUENCY | REDRAW_AREA);
}

UI_FUNCTION_ADV_CALLBACK(menu_transform_filter_acb) {
  if (b) {
    b->icon = (props_mode & TD_FUNC) == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    return;
  }
  props_mode = (props_mode & ~TD_FUNC) | data;
}




#ifdef __USE_SMOOTH__
UI_FUNCTION_ADV_CALLBACK(menu_smooth_sel_acb) {
  (void)data;
  if (b)
    return;
  menu_push_submenu(menu_build_smooth_menu());
}
#endif



UI_FUNCTION_ADV_CALLBACK(menu_power_sel_acb) {
  (void)data;
  if (b) {
    if (current_props._power != SI5351_CLK_DRIVE_STRENGTH_AUTO)
      plot_printf(b->label, sizeof(b->label), "POWER" R_LINK_COLOR "  %um" S_AMPER,
                  2 + current_props._power * 2);
    return;
  }
  menu_push_submenu(menu_build_power_menu());
}

UI_FUNCTION_ADV_CALLBACK(menu_power_acb) {
  if (b) {
    b->icon = current_props._power == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = 2 + data * 2;
    return;
  }
  set_power(data);
}

// Process keyboard button callback, and run keyboard function

UI_FUNCTION_ADV_CALLBACK(menu_keyboard_acb) {
  if (data == KM_VAR &&
      lever_mode == LM_EDELAY) // JOG STEP button auto set (e-delay or frequency step)
    data = KM_VAR_DELAY;
  if (b) {
    ui_keyboard_cb(data, b);
    return;
  }
  ui_mode_keypad(data);
}

// Custom keyboard menu button callback (depend from current trace)
UI_FUNCTION_ADV_CALLBACK(menu_scale_keyboard_acb) {
  // Not apply amplitude / scale / ref for invalid or polar graph
  if (current_trace == TRACE_INVALID)
    return;
  uint32_t type_mask = 1 << trace[current_trace].type;
  if ((type_mask & ROUND_GRID_MASK) && data != KM_SCALE)
    return;
  // Nano scale values
  uint32_t nano_keyb_type = (1 << KM_TOP) | (1 << KM_BOTTOM) | (1 << KM_SCALE);
  if ((type_mask & NANO_TYPE_MASK) && ((1 << data) & nano_keyb_type))
    data++;
  menu_keyboard_acb(data, b);
}

//
// Auto scale active trace
// Calculate reference and scale values depend from max and min trace values (aligning with
// 'beautiful' borders)
//
UI_FUNCTION_CALLBACK(menu_auto_scale_cb) {
  (void)data;
  if (current_trace == TRACE_INVALID || sweep_points == 0)
    return;
  int type = trace[current_trace].type;
  get_value_cb_t c = trace_info_list[type].get_value_cb; // Get callback for value calculation
  if (c == NULL)
    return; // No callback, skip

  float (*array)[2] = measured[trace[current_trace].channel];
  float min_val, max_val;

  // Initialize with the first point
  float v = c(0, array[0]);
  if (vna_fabsf(v) == infinityf())
    return;
  min_val = max_val = v;

  // Find min and max in one pass with fewer comparisons
  int i;
  for (i = 1; i < sweep_points - 1; i += 2) {
    float v1 = c(i, array[i]);
    float v2 = c(i + 1, array[i + 1]);
    if (vna_fabsf(v1) == infinityf() || vna_fabsf(v2) == infinityf())
      return;

    if (v1 < v2) {
      if (v1 < min_val)
        min_val = v1;
      if (v2 > max_val)
        max_val = v2;
    } else {
      if (v2 < min_val)
        min_val = v2;
      if (v1 > max_val)
        max_val = v1;
    }
  }

  // Process the last element if the number of points is odd
  if (i < sweep_points) {
    v = c(i, array[i]);
    if (vna_fabsf(v) == infinityf())
      return;
    if (v < min_val)
      min_val = v;
    if (v > max_val)
      max_val = v;
  }

  const float N = NGRIDY;                 // Grid count
  float delta = max_val - min_val;        // delta
  float mid = (max_val + min_val) * 0.5f; // middle point (align around it)
  if (min_val != max_val)
    delta *= 1.1f; // if max != min use 5% margins
  else if (min_val == 0.0f)
    delta = 2.0f; // on zero use fixed delta
  else
    delta = vna_fabsf(min_val) * 1.2f;  // use 10% margin from value
  float nice_step = 1.0f, temp = delta; // Search best step
  while (temp < 1.0f) {
    temp *= 10.0f;
    nice_step *= 0.1f;
  }
  while (temp >= 10.0f) {
    temp *= 0.1f;
    nice_step *= 10.0f;
  }
  delta *= 2.0f / N;
  while (delta < nice_step)
    nice_step /= 2.0f; // Search substep (grid scale)
  if (type == TRC_SWR)
    mid -= 1.0f; // Hack for SWR trace!
  set_trace_scale(current_trace, nice_step);
  set_trace_refpos(current_trace, (N / 2.0f) - ((int32_t)(mid / nice_step + 0.5f)));
  ui_mode_normal();
}



UI_FUNCTION_ADV_CALLBACK(menu_pause_acb) {
  (void)data;
  if (b) {
    b->icon = (sweep_mode & SWEEP_ENABLE) ? BUTTON_ICON_PAUSE : BUTTON_ICON_PLAY;
    return;
  }
  toggle_sweep();
}

UI_FUNCTION_ADV_CALLBACK(menu_smooth_acb) {
  (void)data;
  if (b) {
     // Check logic for smooth off? For now simple check
     // If complex, just return none
     return;
  }
}








//                                 UI menus
//=====================================================================================================
UI_FUNCTION_CALLBACK(menu_back_cb) {
  (void)data;
  menu_move_back(false);
}

// Back button submenu list
const menuitem_t menu_back[] = {
    {MT_CALLBACK, 0, S_LARROW " BACK", menu_back_cb}, {MT_NEXT, 0, NULL, NULL} // sentinel
};



















const menuitem_t menu_transform[] = {
    {MT_ADV_CALLBACK, 0, "TRANSFORM\n%s", menu_transform_acb},
    {MT_ADV_CALLBACK, TD_FUNC_LOWPASS_IMPULSE, "LOW PASS\nIMPULSE", menu_transform_filter_acb},
    {MT_ADV_CALLBACK, TD_FUNC_LOWPASS_STEP, "LOW PASS\nSTEP", menu_transform_filter_acb},
    {MT_ADV_CALLBACK, TD_FUNC_BANDPASS, "BANDPASS", menu_transform_filter_acb},
    {MT_ADV_CALLBACK, 0, "WINDOW\n " R_LINK_COLOR "%s", menu_transform_window_acb},
    {MT_ADV_CALLBACK, KM_VELOCITY_FACTOR, "VELOCITY F.\n " R_LINK_COLOR "%d%%%%",
     menu_keyboard_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};


static const menu_descriptor_t menu_power_desc[] = {
    {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_2MA},
    {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_4MA},
    {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_6MA},
    {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_8MA},
};

#ifdef __USE_SMOOTH__
static const menu_descriptor_t menu_smooth_desc[] = {
    {MT_ADV_CALLBACK, 1},
    {MT_ADV_CALLBACK, 2},
    {MT_ADV_CALLBACK, 4},
    {MT_ADV_CALLBACK, 5},
    {MT_ADV_CALLBACK, 6},
};
#endif



const menuitem_t menu_measure_tools[] = {
    {MT_SUBMENU, 0, "TRANSFORM", menu_transform},
#ifdef __USE_SMOOTH__
    {MT_ADV_CALLBACK, 0, "DATA\nSMOOTH", menu_smooth_sel_acb},
#endif
#ifdef __VNA_MEASURE_MODULE__
    {MT_CALLBACK, 0, "MEASURE", menu_measure_cb},
#endif
    {MT_ADV_CALLBACK, 0, "IF BANDWIDTH\n " R_LINK_COLOR "%u" S_Hz, menu_bandwidth_sel_acb},
#ifdef __VNA_Z_RENORMALIZATION__
    {MT_ADV_CALLBACK, KM_Z_PORT, "PORT-Z\n " R_LINK_COLOR "50 " S_RARROW " %bF" S_OHM,
     menu_keyboard_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

enum {
  MENU_DYNAMIC_SAVE_SIZE = 10,
  MENU_DYNAMIC_BANDWIDTH_SIZE = 10,
  MENU_DYNAMIC_POINTS_SIZE = 20,
  MENU_DYNAMIC_MARKER_SIZE = 20,
  MENU_DYNAMIC_SERIAL_SPEED_SIZE = 11,
  MENU_DYNAMIC_POWER_SIZE = 1 + ARRAY_COUNT(menu_power_desc) + 1,
#ifdef __USE_SMOOTH__
  MENU_DYNAMIC_SMOOTH_SIZE = 2 + ARRAY_COUNT(menu_smooth_desc) + 1,
#endif
};

#define MENU_MAX(a, b) ((a) > (b) ? (a) : (b))
#define MENU_DYNAMIC_BUFFER_BASE                                                                                      \
  MENU_MAX(MENU_DYNAMIC_SAVE_SIZE,                                                                                    \
           MENU_MAX(MENU_DYNAMIC_BANDWIDTH_SIZE,                                                                      \
                   MENU_MAX(MENU_DYNAMIC_POINTS_SIZE,                                                                 \
                            MENU_MAX(MENU_DYNAMIC_MARKER_SIZE,                                                 \
                                                        MENU_MAX(MENU_DYNAMIC_SERIAL_SPEED_SIZE,                       \
                                                                 MENU_DYNAMIC_POWER_SIZE)))))
#ifdef __USE_SMOOTH__
#define MENU_DYNAMIC_BUFFER_SIZE MENU_MAX(MENU_DYNAMIC_BUFFER_BASE, MENU_DYNAMIC_SMOOTH_SIZE)
#else
#define MENU_DYNAMIC_BUFFER_SIZE MENU_DYNAMIC_BUFFER_BASE
#endif

static menuitem_t* menu_dynamic_buffer;

menuitem_t* menu_dynamic_acquire(void) {
  if (menu_dynamic_buffer == NULL) {
    menu_dynamic_buffer =
        chCoreAllocAligned(MENU_DYNAMIC_BUFFER_SIZE * sizeof(menuitem_t), PORT_NATURAL_ALIGN);
    if (menu_dynamic_buffer == NULL)
      chSysHalt("menu buffer");
  }
  return menu_dynamic_buffer;
}
#undef MENU_DYNAMIC_BUFFER_BASE
#undef MENU_MAX











const menuitem_t* menu_build_power_menu(void) {
  menuitem_t* cursor = menu_dynamic_acquire();
  *cursor++ =
      (menuitem_t){MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_AUTO, "AUTO", menu_power_acb};
  cursor = ui_menu_list(menu_power_desc, ARRAY_COUNT(menu_power_desc), "%u m" S_AMPER,
                        menu_power_acb, cursor);
  menu_set_next(cursor, menu_back);
  return menu_dynamic_buffer;
}

#ifdef __USE_SMOOTH__
const menuitem_t* menu_build_smooth_menu(void) {
  menuitem_t* cursor = menu_dynamic_acquire();
  *cursor++ = (menuitem_t){MT_ADV_CALLBACK, VNA_MODE_SMOOTH, "SMOOTH\n " R_LINK_COLOR "%s avg",
                           menu_vna_mode_acb};
  *cursor++ = (menuitem_t){MT_ADV_CALLBACK, 0, "SMOOTH\nOFF", menu_smooth_acb};
  cursor = ui_menu_list(menu_smooth_desc, ARRAY_COUNT(menu_smooth_desc), "x%d", menu_smooth_acb,
                        cursor);
  menu_set_next(cursor, menu_back);
  return menu_dynamic_buffer;
}
#endif







const menuitem_t menu_top[] = {
    {MT_SUBMENU, 0, "CAL", menu_cal_menu},
    {MT_SUBMENU, 0, "STIMULUS", menu_stimulus},
    {MT_SUBMENU, 0, "DISPLAY", menu_display},
    {MT_SUBMENU, 0, "MEASURE", menu_measure_tools},
#ifdef __USE_SD_CARD__
    {MT_SUBMENU, 0, "SD CARD", menu_sdcard},
#endif
    {MT_SUBMENU, 0, "SYSTEM", menu_system},
    {MT_ADV_CALLBACK, 0, "%s\nSWEEP", menu_pause_acb},
    {MT_NEXT, 0, NULL, NULL} // sentinel
};

#define MENU_STACK_DEPTH_MAX 5
const menuitem_t* menu_stack[MENU_STACK_DEPTH_MAX];

static void menu_stack_reset(void) {
  menu_stack[0] = menu_top;
  for (size_t i = 1; i < MENU_STACK_DEPTH_MAX; i++) {
    menu_stack[i] = NULL;
  }
  menu_current_level = 0;
  selection = -1;
  menu_button_height = MENU_BUTTON_HEIGHT(MENU_BUTTON_MIN);
}

static const menuitem_t* menu_next_item(const menuitem_t* m) {
  if (m == NULL)
    return NULL;
  m++; // Next item
  return m->type == MT_NEXT ? (menuitem_t*)m->reference : m;
}

static const menuitem_t* current_menu_item(int i) {
  const menuitem_t* m = menu_stack[menu_current_level];
  while (i--)
    m = menu_next_item(m);
  return m;
}

static int current_menu_get_count(void) {
  int i = 0;
  const menuitem_t* m = menu_stack[menu_current_level];
  while (m) {
    m = menu_next_item(m);
    i++;
  }
  return i;
}

int get_lines_count(const char* label) {
  int n = 1;
  while (*label)
    if (*label++ == '\n')
      n++;
  return n;
}

static void ensure_selection(void) {
  int i = current_menu_get_count();
  if (selection < 0)
    selection = -1;
  else if (selection >= i)
    selection = i - 1;
  if (i < MENU_BUTTON_MIN)
    i = MENU_BUTTON_MIN;
  else if (i >= MENU_BUTTON_MAX)
    i = MENU_BUTTON_MAX;
  menu_button_height = MENU_BUTTON_HEIGHT(i);
}

void menu_move_back(bool leave_ui) {
  if (menu_current_level == 0)
    return;
  menu_current_level--;
  ensure_selection();
  if (leave_ui)
    ui_mode_normal();
}

void menu_set_submenu(const menuitem_t* submenu) {
  menu_stack[menu_current_level] = submenu;
  ensure_selection();
}

void menu_push_submenu(const menuitem_t* submenu) {
  if (menu_current_level < MENU_STACK_DEPTH_MAX - 1)
    menu_current_level++;
  menu_set_submenu(submenu);
}

/*
static void
menu_move_top(void)
{
  if (menu_current_level == 0)
    return;
  menu_current_level = 0;
  ensure_selection();
}
*/

static void menu_invoke(int item) {
  const menuitem_t* menu = current_menu_item(item);
  if (menu == NULL)
    return;
  switch (menu->type) {
  case MT_CALLBACK:
    if (menu->reference)
      ((menuaction_cb_t)menu->reference)(menu->data);
    break;

  case MT_ADV_CALLBACK:
    if (menu->reference)
      ((menuaction_acb_t)menu->reference)(menu->data, NULL);
    break;

  case MT_SUBMENU:
    menu_push_submenu((const menuitem_t*)menu->reference);
    break;
  }
  // Redraw menu after if UI in menu mode
  if (ui_mode == UI_MENU)
    menu_draw(-1);
}

//=====================================================================================================
//                                      UI Menu processing
//=====================================================================================================
static void menu_draw_buttons(const menuitem_t* m, uint32_t mask) {
  int i;
  int y = MENU_BUTTON_Y_OFFSET;
  for (i = 0; i < MENU_BUTTON_MAX && m; i++, m = menu_next_item(m), y += menu_button_height) {
    if ((mask & (1 << i)) == 0)
      continue;
    button_t button;
    button.fg = LCD_MENU_TEXT_COLOR;
    button.icon = BUTTON_ICON_NONE;
    // focus only in MENU mode but not in KEYPAD mode
    if (ui_mode == UI_MENU && i == selection) {
      button.bg = LCD_MENU_ACTIVE_COLOR;
      button.border = MENU_BUTTON_BORDER | BUTTON_BORDER_FALLING;
    } else {
      button.bg = LCD_MENU_COLOR;
      button.border = MENU_BUTTON_BORDER | BUTTON_BORDER_RISE;
    }
    // Custom button, apply custom settings/label from callback
    const char* text;
    uint16_t text_offs;
    if (m->type == MT_ADV_CALLBACK) {
      button.label[0] = 0;
      if (m->reference)
        ((menuaction_acb_t)m->reference)(m->data, &button);
      // Apply custom text, from button label and
      if (button.label[0] == 0)
        plot_printf(button.label, sizeof(button.label), m->label, button.p1.u);
      text = button.label;
    } else
      text = m->label;
    // Draw button
    ui_draw_button(LCD_WIDTH - MENU_BUTTON_WIDTH, y, MENU_BUTTON_WIDTH, menu_button_height,
                   &button);
    // Draw icon if need (and add extra shift for text)
    if (button.icon >= 0) {
      lcd_blit_bitmap(LCD_WIDTH - MENU_BUTTON_WIDTH + MENU_BUTTON_BORDER + MENU_ICON_OFFSET,
                      y + (menu_button_height - ICON_HEIGHT) / 2, ICON_WIDTH, ICON_HEIGHT,
                      ICON_GET_DATA(button.icon));
      text_offs = LCD_WIDTH - MENU_BUTTON_WIDTH + MENU_BUTTON_BORDER + MENU_ICON_OFFSET + ICON_SIZE;
    } else
      text_offs = LCD_WIDTH - MENU_BUTTON_WIDTH + MENU_BUTTON_BORDER + MENU_TEXT_OFFSET;
    // Draw button text
    int lines = get_lines_count(text);
#if _USE_FONT_ != _USE_SMALL_FONT_
    if (menu_button_height < lines * FONT_GET_HEIGHT + 2) {
      lcd_set_font(FONT_SMALL);
      lcd_drawstring(text_offs, y + (menu_button_height - lines * sFONT_STR_HEIGHT - 1) / 2, text);
    } else {
      lcd_set_font(FONT_NORMAL);
      lcd_printf(
          text_offs,
          y + (menu_button_height - lines * FONT_STR_HEIGHT + (FONT_STR_HEIGHT - FONT_GET_HEIGHT)) /
                  2,
          text);
    }
#else
    lcd_printf(
        text_offs,
        y + (menu_button_height - lines * FONT_STR_HEIGHT + (FONT_STR_HEIGHT - FONT_GET_HEIGHT)) /
                2,
        text);
#endif
  }
  // Erase empty buttons
  if (AREA_HEIGHT_NORMAL + OFFSETY > y) {
    lcd_set_background(LCD_BG_COLOR);
    lcd_fill(LCD_WIDTH - MENU_BUTTON_WIDTH, y, MENU_BUTTON_WIDTH, AREA_HEIGHT_NORMAL + OFFSETY - y);
  }
  lcd_set_font(FONT_NORMAL);
}

static void menu_draw(uint32_t mask) {
  menu_draw_buttons(menu_stack[menu_current_level], mask);
}

#if 0
static void erase_menu_buttons(void) {
  lcd_set_background(LCD_BG_COLOR);
  lcd_fill(LCD_WIDTH-MENU_BUTTON_WIDTH, 0, MENU_BUTTON_WIDTH, MENU_BUTTON_HEIGHT*MENU_BUTTON_MAX);
}
#endif

//  Menu mode processing
static void ui_mode_menu(void) {
  if (ui_mode == UI_MENU)
    return;

  ui_mode = UI_MENU;
  // narrowen plotting area
  set_area_size(AREA_WIDTH_NORMAL - MENU_BUTTON_WIDTH, AREA_HEIGHT_NORMAL);
  ensure_selection();
  menu_draw(-1);
}

static void ui_menu_lever(uint16_t status) {
  uint16_t count = current_menu_get_count();
  if (status & EVT_BUTTON_SINGLE_CLICK) {
    if ((uint16_t)selection >= count)
      ui_mode_normal();
    else
      menu_invoke(selection);
    return;
  }

  do {
    uint32_t mask = 1 << selection;
    if (status & EVT_UP)
      selection++;
    if (status & EVT_DOWN)
      selection--;
    // close menu if no menu item
    if ((uint16_t)selection >= count) {
      ui_mode_normal();
      return;
    }
    menu_draw(mask | (1 << selection));
    chThdSleepMilliseconds(100);
  } while ((status = ui_input_wait_release()) != 0);
}

static void ui_menu_touch(int touch_x, int touch_y) {
  if (LCD_WIDTH - MENU_BUTTON_WIDTH < touch_x) {
    int16_t i = (touch_y - MENU_BUTTON_Y_OFFSET) / menu_button_height;
    if ((uint16_t)i < (uint16_t)current_menu_get_count()) {
      uint32_t mask = (1 << i) | (1 << selection);
      selection = i;
      menu_draw(mask);
      touch_wait_release();
      selection = -1;
      menu_invoke(i);
      return;
    }
  }

  touch_wait_release();
  ui_mode_normal();
}
//====================================== end menu processing
//==========================================

//=====================================================================================================
//                                  KEYBOARD macros, variables
//=====================================================================================================

//=====================================================================================================
//                                      KEYBOARD functions
//=====================================================================================================













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

static void ui_normal_lever(uint16_t status) {
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

static void ui_normal_touch(int touch_x, int touch_y) {
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

static const struct {
  void (*button)(uint16_t status);
  void (*touch)(int touch_x, int touch_y);
} ui_handler[] = {
    [UI_NORMAL] = {ui_normal_lever, ui_normal_touch},
    [UI_MENU] = {ui_menu_lever, ui_menu_touch},
    [UI_KEYPAD] = {ui_keypad_lever, ui_keypad_touch},
#ifdef __SD_FILE_BROWSER__
    [UI_BROWSER] = {ui_browser_lever, ui_browser_touch},
#endif
};

static void ui_process_lever(void) {
  uint16_t status = ui_input_check();
  if (status)
    ui_handler[ui_mode].button(status);
}

static void ui_process_touch(void) {
  int touch_x, touch_y;
  int status = touch_check();
  if (status == EVT_TOUCH_PRESSED || status == EVT_TOUCH_DOWN) {
    touch_position(&touch_x, &touch_y);
    ui_handler[ui_mode].touch(touch_x, touch_y);
  }
}

void ui_process(void) {
  ui_controller_dispatch_board_events();
  uint8_t requests = ui_controller_acquire_requests(UI_CONTROLLER_REQUEST_LEVER |
                                                    UI_CONTROLLER_REQUEST_TOUCH);
  if (requests & UI_CONTROLLER_REQUEST_LEVER) {
    ui_process_lever();
  }
  if (requests & UI_CONTROLLER_REQUEST_TOUCH) {
    ui_process_touch();
  }

  touch_start_watchdog();
}

void handle_button_interrupt(uint16_t channel) {
  ui_controller_publish_board_event(BOARD_EVENT_BUTTON, channel, true);
}

// static systime_t t_time = 0;
//  Triggered touch interrupt call
void handle_touch_interrupt(void) {
  ui_controller_publish_board_event(BOARD_EVENT_TOUCH, 0, true);
}

#if HAL_USE_EXT == TRUE // Use ChibiOS EXT code (need lot of flash ~1.5k)
static void handle_button_ext(EXTDriver* extp, expchannel_t channel) {
  (void)extp;
  handle_button_interrupt((uint16_t)channel);
}

static const EXTConfig extcfg = {
    {{EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_RISING_EDGE | EXT_CH_MODE_AUTOSTART | EXT_MODE_GPIOA, handle_button_ext}, // EXT1
     {EXT_CH_MODE_RISING_EDGE | EXT_CH_MODE_AUTOSTART | EXT_MODE_GPIOA, handle_button_ext}, // EXT2
     {EXT_CH_MODE_RISING_EDGE | EXT_CH_MODE_AUTOSTART | EXT_MODE_GPIOA, handle_button_ext}, // EXT3
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL},
     {EXT_CH_MODE_DISABLED, NULL}}};

static void ui_init_ext(void) {
  extStart(&EXTD1, &extcfg);
}
#else // Use custom EXT lib, allow save flash (but need fix for different CPU)
static void ui_init_ext(void) {
  // Activates the EXT driver 1.
  extStart();
  ext_channel_enable(1, EXT_CH_MODE_RISING_EDGE | EXT_MODE_GPIOA);
  ext_channel_enable(2, EXT_CH_MODE_RISING_EDGE | EXT_MODE_GPIOA);
  ext_channel_enable(3, EXT_CH_MODE_RISING_EDGE | EXT_MODE_GPIOA);
}
#endif

void ui_init() {
  menu_stack_reset();
  ui_input_reset_state();
  // Activates the EXT driver 1.
  ui_init_ext();
  // Init touch subsystem
  touch_init();
  // Set LCD display brightness
#ifdef __LCD_BRIGHTNESS__
  lcd_set_brightness(config._brightness);
#endif
}
