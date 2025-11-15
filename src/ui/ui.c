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
#include "app/sweep_service.h"
#include "app/shell.h"
#include "system/state_manager.h"
#include "hal.h"
#include "services/event_bus.h"
#include "services/config_service.h"
#include "chprintf.h"
#include <string.h>
#include "si5351.h"
#include "ui/input_adapters/hardware_input.h"
#include "ui/ui_internal.h"
#include "ui/sd_browser.h"

// Use size optimization (UI not need fast speed, better have smallest size)
#pragma GCC optimize("Os")

#define TOUCH_INTERRUPT_ENABLED 1

// Cooperative polling budgets for constrained UI loop. The sweep thread must
// yield within 16 ms to keep the display responsive on the STM32F303/F072
// boards (72 MHz Cortex-M4 or 48 MHz Cortex-M0+ with tight SRAM). Keep touch
// polling slices comfortably below this bound.
#define TOUCH_RELEASE_POLL_INTERVAL_MS 2U // 500 Hz release detection
#define TOUCH_DRAG_POLL_INTERVAL_MS 8U    // 125 Hz drag updates
static uint8_t touch_status_flag = 0;
static uint8_t last_touch_status = EVT_TOUCH_NONE;
static int16_t last_touch_x;
static int16_t last_touch_y;
uint8_t operation_requested = OP_NONE;

static event_bus_t* ui_event_bus = NULL;

static void ui_on_event(const event_bus_message_t* message, void* user_data);

typedef struct {
  uint16_t mask;
  systime_t next_tick;
} lever_repeat_state_t;

static lever_repeat_state_t lever_repeat_state = {0, 0};

static inline uint16_t buttons_to_event_mask(uint16_t buttons) {
  uint16_t mask = 0;
  if (buttons & BUTTON_DOWN) {
    mask |= EVT_DOWN;
  }
  if (buttons & BUTTON_UP) {
    mask |= EVT_UP;
  }
  return mask;
}

bool ui_lever_repeat_pending(void) {
  return lever_repeat_state.mask != 0U;
}

void ui_attach_event_bus(event_bus_t* bus) {
  if (ui_event_bus == bus) {
    return;
  }
  ui_event_bus = bus;
  if (bus != NULL) {
    event_bus_subscribe(bus, EVENT_SWEEP_STARTED, ui_on_event, NULL);
    event_bus_subscribe(bus, EVENT_SWEEP_COMPLETED, ui_on_event, NULL);
    event_bus_subscribe(bus, EVENT_STORAGE_UPDATED, ui_on_event, NULL);
  }
}

static void ui_on_event(const event_bus_message_t* message, void* user_data) {
  (void)user_data;
  if (message == NULL) {
    return;
  }
  switch (message->topic) {
  case EVENT_SWEEP_STARTED:
    request_to_redraw(REDRAW_BATTERY);
    break;
  case EVENT_SWEEP_COMPLETED:
    request_to_redraw(REDRAW_PLOT | REDRAW_BATTERY);
    break;
  case EVENT_STORAGE_UPDATED:
    request_to_redraw(REDRAW_CAL_STATUS);
    break;
  default:
    break;
  }
}

//==============================================
static uint16_t menu_button_height = MENU_BUTTON_HEIGHT(MENU_BUTTON_MIN);

enum {
  UI_NORMAL,
  UI_MENU,
  UI_KEYPAD,
#if SD_BROWSER_ENABLED
  UI_BROWSER,
#endif
};

#ifdef __USE_SD_CARD__
static uint8_t keyboard_temp; // Used for SD card keyboard workflows
#endif

// Keypad structures
// Enum for keypads_list
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

typedef struct {
  uint16_t x_offs;
  uint16_t y_offs;
  uint16_t width;
  uint16_t height;
} keypad_pos_t;

typedef struct {
  uint8_t size, type;
  struct {
    uint8_t pos;
    uint8_t c;
  } buttons[];
} keypads_t;

typedef void (*keyboard_cb_t)(uint16_t data, button_t* b);
#define UI_KEYBOARD_CALLBACK(ui_kb_function_name)                                                  \
  void ui_kb_function_name(uint16_t data, button_t* b)

typedef struct {
  uint8_t keypad_type;
  uint8_t data;
  const char* name;
  const keyboard_cb_t cb;
} __attribute__((packed)) keypads_list;

// Max keyboard input length
#define NUMINPUT_LEN 12
#define TXTINPUT_LEN (8)

#if NUMINPUT_LEN + 2 > TXTINPUT_LEN + 1
static char kp_buf[NUMINPUT_LEN +
                   2]; // !!!!!! WARNING size must be + 2 from NUMINPUT_LEN or TXTINPUT_LEN + 1
#else
static char kp_buf[TXTINPUT_LEN +
                   1]; // !!!!!! WARNING size must be + 2 from NUMINPUT_LEN or TXTINPUT_LEN + 1
#endif
static uint8_t ui_mode = UI_NORMAL;
static const keypads_t* keypads;
static uint8_t keypad_mode;
static uint8_t menu_current_level = 0;
static int8_t selection = -1;

// UI menu structure
// Type of menu item:

// Button definition (used in MT_ADV_CALLBACK for custom)
void ui_mode_normal(void);
static void ui_mode_menu(void);
static void ui_mode_keypad(int _keypad_mode);

static void menu_draw(uint32_t mask);
static void menu_move_back(bool leave_ui);
static void menu_push_submenu(const menuitem_t* submenu);
static void menu_set_submenu(const menuitem_t* submenu);

// Icons for UI
#include "../resources/icons/icons_menu.c"

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

#define SOFTWARE_TOUCH
//*******************************************************************************
// Software Touch module
//*******************************************************************************
#ifdef SOFTWARE_TOUCH
static int touch_measure_y(void) {
  // drive low to high on X line (At this state after touch_prepare_sense)
  //  palSetPadMode(GPIOB, GPIOB_XN, PAL_MODE_OUTPUT_PUSHPULL); //
  //  palSetPadMode(GPIOA, GPIOA_XP, PAL_MODE_OUTPUT_PUSHPULL); //
  // drive low to high on X line (coordinates from top to bottom)
  palClearPad(GPIOB, GPIOB_XN);
  //  palSetPad(GPIOA, GPIOA_XP);

  // open Y line (At this state after touch_prepare_sense)
  //  palSetPadMode(GPIOB, GPIOB_YN, PAL_MODE_INPUT);        // Hi-z mode
  palSetPadMode(GPIOA, GPIOA_YP, PAL_MODE_INPUT_ANALOG); // <- ADC_TOUCH_Y channel

  //  chThdSleepMilliseconds(3);
  return adc_single_read(ADC_TOUCH_Y);
}

static int touch_measure_x(void) {
  // drive high to low on Y line (coordinates from left to right)
  palSetPad(GPIOB, GPIOB_YN);
  palClearPad(GPIOA, GPIOA_YP);
  // Set Y line as output
  palSetPadMode(GPIOB, GPIOB_YN, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOA, GPIOA_YP, PAL_MODE_OUTPUT_PUSHPULL);
  // Set X line as input
  palSetPadMode(GPIOB, GPIOB_XN, PAL_MODE_INPUT);        // Hi-z mode
  palSetPadMode(GPIOA, GPIOA_XP, PAL_MODE_INPUT_ANALOG); // <- ADC_TOUCH_X channel
                                                         //  chThdSleepMilliseconds(3);
  return adc_single_read(ADC_TOUCH_X);
}
// Manually measure touch event
static inline int touch_status(void) {
  return adc_single_read(ADC_TOUCH_Y) > TOUCH_THRESHOLD;
}

static void touch_prepare_sense(void) {
  // Set Y line as input
  palSetPadMode(GPIOB, GPIOB_YN, PAL_MODE_INPUT);          // Hi-z mode
  palSetPadMode(GPIOA, GPIOA_YP, PAL_MODE_INPUT_PULLDOWN); // Use pull
  // drive high on X line (for touch sense on Y)
  palSetPad(GPIOB, GPIOB_XN);
  palSetPad(GPIOA, GPIOA_XP);
  // force high X line
  palSetPadMode(GPIOB, GPIOB_XN, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOA, GPIOA_XP, PAL_MODE_OUTPUT_PUSHPULL);
  //  chThdSleepMilliseconds(10); // Wait 10ms for denounce touch
}

#ifdef __REMOTE_DESKTOP__
static uint8_t touch_remote = REMOTE_NONE;
void remote_touch_set(uint16_t state, int16_t x, int16_t y) {
  touch_remote = state;
  if (x != -1)
    last_touch_x = x;
  if (y != -1)
    last_touch_y = y;
  handle_touch_interrupt();
}
#endif

static void touch_start_watchdog(void) {
  if (touch_status_flag & TOUCH_INTERRUPT_ENABLED)
    return;
  touch_status_flag ^= TOUCH_INTERRUPT_ENABLED;
  adc_start_analog_watchdog();
#ifdef __REMOTE_DESKTOP__
  touch_remote = REMOTE_NONE;
#endif
}

static void touch_stop_watchdog(void) {
  if (!(touch_status_flag & TOUCH_INTERRUPT_ENABLED))
    return;
  touch_status_flag ^= TOUCH_INTERRUPT_ENABLED;
  adc_stop_analog_watchdog();
}

// Touch panel timer check (check press frequency 20Hz)
#if HAL_USE_GPT == TRUE
static const GPTConfig gpt3cfg = {1000,   // 1kHz timer clock.
                                  NULL,   // Timer callback.
                                  0x0020, // CR2:MMS=02 to output TRGO
                                  0};

static void touch_init_timers(void) {
  gptStart(&GPTD3, &gpt3cfg);     // Init timer 3
  gptStartContinuous(&GPTD3, 10); // Start timer 10ms period (use 1kHz clock)
}
#else
static void touch_init_timers(void) {
  board_init_timers();
  board_start_timer(TIM3, 10); // Start timer 10ms period (use 1kHz clock)
}
#endif

//
// Touch init function init timer 3 trigger adc for check touch interrupt, and run measure
//
static void touch_init(void) {
  // Prepare pin for measure touch event
  touch_prepare_sense();
  // Start touch interrupt, used timer_3 ADC check threshold:
  touch_init_timers();
  touch_start_watchdog(); // Start ADC watchdog (measure by timer 3 interval and trigger interrupt
                          // if touch pressed)
}

// Main software touch function, should:
// set last_touch_x and last_touch_x
// return touch status
int touch_check(void) {
  touch_stop_watchdog();

  int stat = touch_status();
  if (stat) {
    int y = touch_measure_y();
    int x = touch_measure_x();
    touch_prepare_sense();
    if (touch_status()) {
      last_touch_x = x;
      last_touch_y = y;
    }
#ifdef __REMOTE_DESKTOP__
    touch_remote = REMOTE_NONE;
  } else {
    stat = touch_remote == REMOTE_PRESS;
#endif
  }

  if (stat != last_touch_status) {
    last_touch_status = stat;
    return stat ? EVT_TOUCH_PRESSED : EVT_TOUCH_RELEASED;
  }
  return stat ? EVT_TOUCH_DOWN : EVT_TOUCH_NONE;
}
//*******************************************************************************
// End Software Touch module
//*******************************************************************************
#endif // end SOFTWARE_TOUCH

//*******************************************************************************
//                           UI functions
//*******************************************************************************
void touch_wait_release(void) {
  while (touch_check() != EVT_TOUCH_RELEASED) {
    chThdSleepMilliseconds(TOUCH_RELEASE_POLL_INTERVAL_MS);
  }
}

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

static void get_touch_point(uint16_t x, uint16_t y, const char* name, int16_t* data) {
  // Clear screen and ask for press
  lcd_set_colors(LCD_FG_COLOR, LCD_BG_COLOR);
  lcd_clear_screen();
  lcd_blit_bitmap(x, y, TOUCH_MARK_W, TOUCH_MARK_H, (const uint8_t*)touch_bitmap);
  lcd_printf((LCD_WIDTH - FONT_STR_WIDTH(18)) / 2, (LCD_HEIGHT - FONT_GET_HEIGHT) / 2, "TOUCH %s *",
             name);
  // Wait release, and fill data
  touch_wait_release();
  data[0] = last_touch_x;
  data[1] = last_touch_y;
}

void ui_touch_cal_exec(void) {
  const uint16_t x1 = CALIBRATION_OFFSET - TOUCH_MARK_X;
  const uint16_t y1 = CALIBRATION_OFFSET - TOUCH_MARK_Y;
  const uint16_t x2 = LCD_WIDTH - 1 - CALIBRATION_OFFSET - TOUCH_MARK_X;
  const uint16_t y2 = LCD_HEIGHT - 1 - CALIBRATION_OFFSET - TOUCH_MARK_Y;
  uint16_t p1 = 0, p2 = 2;
#ifdef __FLIP_DISPLAY__
  if (VNA_MODE(VNA_MODE_FLIP_DISPLAY)) {
    p1 = 2, p2 = 0;
  }
#endif
  get_touch_point(x1, y1, "UPPER LEFT", &config._touch_cal[p1]);
  get_touch_point(x2, y2, "LOWER RIGHT", &config._touch_cal[p2]);
  config_service_notify_configuration_changed();
}

void touch_position(int* x, int* y) {
#ifdef __REMOTE_DESKTOP__
  if (touch_remote != REMOTE_NONE) {
    *x = last_touch_x;
    *y = last_touch_y;
    return;
  }
#endif

  static int16_t cal_cache[4] = {0};
  static int32_t scale_x = 1 << 16;
  static int32_t scale_y = 1 << 16;

  // Check if calibration data has changed and recalculate scales if needed
  if (memcmp(cal_cache, config._touch_cal, sizeof(cal_cache)) != 0) {
    memcpy(cal_cache, config._touch_cal, sizeof(cal_cache));

    int32_t denom_x = config._touch_cal[2] - config._touch_cal[0];
    int32_t denom_y = config._touch_cal[3] - config._touch_cal[1];

    if (denom_x != 0 && denom_y != 0) {
      scale_x = ((int32_t)(LCD_WIDTH - 1 - 2 * CALIBRATION_OFFSET) << 16) / denom_x;
      scale_y = ((int32_t)(LCD_HEIGHT - 1 - 2 * CALIBRATION_OFFSET) << 16) / denom_y;
    } else {
      // Division by zero, keep default scale
    }
  }

  int tx, ty;
  tx = (int)(((int64_t)scale_x * (last_touch_x - config._touch_cal[0])) >> 16) + CALIBRATION_OFFSET;
  if (tx < 0)
    tx = 0;
  else if (tx >= LCD_WIDTH)
    tx = LCD_WIDTH - 1;

  ty = (int)(((int64_t)scale_y * (last_touch_y - config._touch_cal[1])) >> 16) + CALIBRATION_OFFSET;
  if (ty < 0)
    ty = 0;
  else if (ty >= LCD_HEIGHT)
    ty = LCD_HEIGHT - 1;

#ifdef __FLIP_DISPLAY__
  if (VNA_MODE(VNA_MODE_FLIP_DISPLAY)) {
    tx = LCD_WIDTH - 1 - tx;
    ty = LCD_HEIGHT - 1 - ty;
  }
#endif
  *x = tx;
  *y = ty;
}

void ui_touch_draw_test(void) {
  int x0, y0;
  int x1, y1;
  lcd_set_colors(LCD_FG_COLOR, LCD_BG_COLOR);
  lcd_clear_screen();
  lcd_drawstring(OFFSETX, LCD_HEIGHT - FONT_GET_HEIGHT,
                 "TOUCH TEST: DRAG PANEL, PRESS BUTTON TO FINISH");

  while (1) {
    if (ui_input_check() & EVT_BUTTON_SINGLE_CLICK)
      break;
    if (touch_check() == EVT_TOUCH_PRESSED) {
      touch_position(&x0, &y0);
      do {
        lcd_printf(10, 30, "%3d %3d ", x0, y0);
        chThdSleepMilliseconds(50);
        touch_position(&x1, &y1);
        lcd_line(x0, y0, x1, y1);
        x0 = x1;
        y0 = y1;
      } while (touch_check() != EVT_TOUCH_RELEASED);
    }
  }
}


static void ui_show_version(void) {
  int x = 5, y = 5, i = 1;
  int str_height = FONT_STR_HEIGHT + 2;
  lcd_set_colors(LCD_FG_COLOR, LCD_BG_COLOR);

  lcd_clear_screen();
  uint16_t shift = 0b00010010000;
  lcd_drawstring_size(BOARD_NAME, x, y, 3);
  y += FONT_GET_HEIGHT * 3 + 3 - 5;
  while (info_about[i]) {
    do {
      shift >>= 1;
      y += 5;
    } while (shift & 1);
    lcd_drawstring(x, y += str_height - 5, info_about[i++]);
  }
  uint32_t id0 = *(uint32_t*)0x1FFFF7AC; // MCU id0 address
  uint32_t id1 = *(uint32_t*)0x1FFFF7B0; // MCU id1 address
  uint32_t id2 = *(uint32_t*)0x1FFFF7B4; // MCU id2 address
  lcd_printf(x, y += str_height, "SN: %08x-%08x-%08x", id0, id1, id2);
  lcd_printf(x, y += str_height, "TCXO = %q" S_Hz, config._xtal_freq);
  lcd_printf(LCD_WIDTH - FONT_STR_WIDTH(20), LCD_HEIGHT - FONT_STR_HEIGHT - 2,
             SET_FGCOLOR(\x16) "In memory of Maya" SET_FGCOLOR(\x01));
  y += str_height * 2;
  // Update battery and time - limit iterations to allow measurement cycle to continue
  uint16_t cnt = 0;
  uint16_t max_iterations = 500; // Limit iterations to ~20 seconds before returning control
  while (cnt < max_iterations) {
    if (touch_check() == EVT_TOUCH_PRESSED)
      break;
    if (ui_input_check() & EVT_BUTTON_SINGLE_CLICK)
      break;
    chThdSleepMilliseconds(40);
    if ((cnt++) & 0x07)
      continue; // Not update time so fast

#ifdef __USE_RTC__
    uint32_t tr = rtc_get_tr_bin(); // TR read first
    uint32_t dr = rtc_get_dr_bin(); // DR read second
    lcd_printf(x, y,
               "Time: 20%02d/%02d/%02d %02d:%02d:%02d"
               " (LS%c)",
               RTC_DR_YEAR(dr), RTC_DR_MONTH(dr), RTC_DR_DAY(dr), RTC_TR_HOUR(dr), RTC_TR_MIN(dr),
               RTC_TR_SEC(dr), (RCC->BDCR & STM32_RTCSEL_MASK) == STM32_RTCSEL_LSE ? 'E' : 'I');
#endif
#if 1
    uint32_t vbat = adc_vbat_read();
    lcd_printf(x, y + str_height, "Batt: %d.%03d" S_VOLT, vbat / 1000, vbat % 1000);
#endif
  }
}

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

static bool select_lever_mode(int mode) {
  if (lever_mode == mode)
    return false;
  lever_mode = mode;
  request_to_redraw(REDRAW_BACKUP | REDRAW_FREQUENCY | REDRAW_MARKER);
  return true;
}

static UI_FUNCTION_ADV_CALLBACK(menu_calop_acb) {
  static const struct {
    uint8_t mask, next;
  } c_list[5] = {
      [CAL_LOAD] = {CALSTAT_LOAD, 3},   [CAL_OPEN] = {CALSTAT_OPEN, 1},
      [CAL_SHORT] = {CALSTAT_SHORT, 2}, [CAL_THRU] = {CALSTAT_THRU, 6},
      [CAL_ISOLN] = {CALSTAT_ISOLN, 4},
  };
  if (b) {
    if (cal_status & c_list[data].mask)
      b->icon = BUTTON_ICON_CHECK;
    return;
  }
  // Reset the physical button debounce state when advancing through CAL steps
  ui_input_reset_state();
  cal_collect(data);
  selection = c_list[data].next;
}

static UI_FUNCTION_ADV_CALLBACK(menu_cal_enh_acb) {
  (void)data;
  if (b) {
    b->icon = (cal_status & CALSTAT_ENHANCED_RESPONSE) ? BUTTON_ICON_CHECK : BUTTON_ICON_NOCHECK;
    return;
  }
  // toggle applying correction
  cal_status ^= CALSTAT_ENHANCED_RESPONSE;
  request_to_redraw(REDRAW_CAL_STATUS);
}

extern const menuitem_t menu_save[];
static UI_FUNCTION_CALLBACK(menu_caldone_cb) {
  cal_done();
  menu_move_back(false);
  if (data == 0)
    menu_push_submenu(menu_save);
}

static UI_FUNCTION_CALLBACK(menu_cal_reset_cb) {
  (void)data;
  // RESET
  cal_status &= CALSTAT_ENHANCED_RESPONSE; // leave ER state
  lastsaveid = NO_SAVE_SLOT;
  // set_power(SI5351_CLK_DRIVE_STRENGTH_AUTO);
  request_to_redraw(REDRAW_CAL_STATUS);
}

static UI_FUNCTION_ADV_CALLBACK(menu_cal_range_acb) {
  (void)data;
  bool calibrated = cal_status & (CALSTAT_ES | CALSTAT_ER | CALSTAT_ET | CALSTAT_ED | CALSTAT_EX |
                                  CALSTAT_OPEN | CALSTAT_SHORT | CALSTAT_THRU);
  if (!calibrated)
    return;
  if (b) {
    b->bg = (cal_status & CALSTAT_INTERPOLATED) ? LCD_INTERP_CAL_COLOR : LCD_MENU_COLOR;
    plot_printf(b->label, sizeof(b->label), "CAL: %dp\n %.6F" S_Hz "\n %.6F" S_Hz, cal_sweep_points,
                (float)cal_frequency0, (float)cal_frequency1);
    return;
  }
  // Reset range to calibration
  if (cal_status & CALSTAT_INTERPOLATED) {
    reset_sweep_frequency();
    set_power(cal_power);
  }
}

static UI_FUNCTION_ADV_CALLBACK(menu_cal_apply_acb) {
  (void)data;
  if (b) {
    b->icon = (cal_status & CALSTAT_APPLY) ? BUTTON_ICON_CHECK : BUTTON_ICON_NOCHECK;
    return;
  }
  // toggle applying correction
  cal_status ^= CALSTAT_APPLY;
  request_to_redraw(REDRAW_CAL_STATUS);
}

static UI_FUNCTION_ADV_CALLBACK(menu_recall_acb) {
  if (b) {
    const properties_t* p = get_properties(data);
    if (p)
      plot_printf(b->label, sizeof(b->label), "%.6F" S_Hz "\n%.6F" S_Hz, (float)p->_frequency0,
                  (float)p->_frequency1);
    else
      b->p1.u = data;
    if (lastsaveid == data)
      b->icon = BUTTON_ICON_CHECK;
    return;
  }
  load_properties(data);
}

enum {
  MENU_CONFIG_TOUCH_CAL = 0,
  MENU_CONFIG_TOUCH_TEST,
  MENU_CONFIG_VERSION,
  MENU_CONFIG_SAVE,
  MENU_CONFIG_RESET,
#if defined(__SD_CARD_LOAD__) && !SD_BROWSER_ENABLED
  MENU_CONFIG_LOAD,
#endif
};

static UI_FUNCTION_CALLBACK(menu_config_cb) {
  switch (data) {
  case MENU_CONFIG_TOUCH_CAL:
    ui_touch_cal_exec();
    break;
  case MENU_CONFIG_TOUCH_TEST:
    ui_touch_draw_test();
    break;
  case MENU_CONFIG_VERSION:
    ui_show_version();
    break;
  case MENU_CONFIG_SAVE:
    config_save();
    state_manager_force_save();
    menu_move_back(true);
    return;
  case MENU_CONFIG_RESET:
    clear_all_config_prop_data();
    NVIC_SystemReset();
    break;
#if defined(__SD_CARD_LOAD__) && !SD_BROWSER_ENABLED
  case MENU_CONFIG_LOAD:
    if (!sd_card_load_config())
      ui_message_box("Error", "No config.ini", 2000);
    break;
#endif
  }
  ui_mode_normal();
  request_to_redraw(REDRAW_ALL);
}

#ifdef __DFU_SOFTWARE_MODE__
static UI_FUNCTION_CALLBACK(menu_dfu_cb) {
  (void)data;
  ui_enter_dfu();
}
#endif

static UI_FUNCTION_ADV_CALLBACK(menu_save_acb) {
  if (b) {
    const properties_t* p = get_properties(data);
    if (p)
      plot_printf(b->label, sizeof(b->label), "%.6F" S_Hz "\n%.6F" S_Hz, (float)p->_frequency0,
                  (float)p->_frequency1);
    else
      b->p1.u = data;
    return;
  }
  if (caldata_save(data) == 0) {
    menu_move_back(true);
    request_to_redraw(REDRAW_BACKUP | REDRAW_CAL_STATUS);
  }
}

static UI_FUNCTION_ADV_CALLBACK(menu_trace_acb) {
  if (b) {
    if (trace[data].enabled) {
      b->bg = LCD_TRACE_1_COLOR + data;
      if (data == selection)
        b->bg = LCD_MENU_ACTIVE_COLOR;
      if (current_trace == data)
        b->icon = BUTTON_ICON_CHECK;
    }
    b->p1.u = data;
    return;
  }

  if (trace[data].enabled && data != current_trace) // for enabled trace and not current trace
    set_active_trace(data);                         // make active
  else                                              //
    set_trace_enable(data, !trace[data].enabled);   // toggle trace enable
}

extern const menuitem_t menu_trace[];
static UI_FUNCTION_ADV_CALLBACK(menu_traces_acb) {
  (void)data;
  if (b) {
    if (current_trace == TRACE_INVALID)
      return;
    b->bg = LCD_TRACE_1_COLOR + current_trace;
    //    b->p1.u = current_trace;
    return;
  }
  menu_push_submenu(menu_trace);
}

extern const menuitem_t menu_marker_s11smith[];
extern const menuitem_t menu_marker_s21smith[];
extern const menuitem_t menu_marker[];
static uint8_t get_smith_format(void) {
  return (current_trace != TRACE_INVALID) ? trace[current_trace].smith_format : 0;
}

static UI_FUNCTION_ADV_CALLBACK(menu_marker_smith_acb) {
  if (b) {
    b->icon = get_smith_format() == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.text = get_smith_format_names(data);
    return;
  }
  if (current_trace == TRACE_INVALID)
    return;
  trace[current_trace].smith_format = data;
  request_to_redraw(REDRAW_AREA | REDRAW_MARKER);
}

#define F_S11 0x00
#define F_S21 0x80
static UI_FUNCTION_ADV_CALLBACK(menu_format_acb) {
  if (current_trace == TRACE_INVALID)
    return; // Not apply any for invalid traces
  uint16_t format = data & (~F_S21);
  uint16_t channel = data & F_S21 ? 1 : 0;
  if (b) {
    if (trace[current_trace].type == format && trace[current_trace].channel == channel)
      b->icon = BUTTON_ICON_CHECK;
    if (format == TRC_SMITH) {
      uint8_t marker_smith_format = get_smith_format();
      if ((channel == 0 && !S11_SMITH_VALUE(marker_smith_format)) ||
          (channel == 1 && !S21_SMITH_VALUE(marker_smith_format)))
        return;
      plot_printf(b->label, sizeof(b->label), "%s\n" R_LINK_COLOR "%s",
                  get_trace_typename(TRC_SMITH, marker_smith_format),
                  get_smith_format_names(marker_smith_format));
    } else
      b->p1.text = get_trace_typename(format, -1);
    return;
  }

  if (format == TRC_SMITH && trace[current_trace].type == TRC_SMITH &&
      trace[current_trace].channel == channel)
    menu_push_submenu(channel == 0 ? menu_marker_s11smith : menu_marker_s21smith);
  else
    set_trace_type(current_trace, format, channel);
}

static UI_FUNCTION_ADV_CALLBACK(menu_channel_acb) {
  (void)data;
  if (current_trace == TRACE_INVALID) {
    if (b)
      b->p1.text = "";
    return;
  }
  int ch = trace[current_trace].channel;
  if (b) {
    b->p1.text = ch == 0 ? "S11 (REFL)" : "S21 (THRU)";
    return;
  }
  // Change channel only if trace type available for this
  if ((1 << (trace[current_trace].type)) & S11_AND_S21_TYPE_MASK)
    set_trace_channel(current_trace, ch ^ 1);
}

static UI_FUNCTION_ADV_CALLBACK(menu_transform_window_acb) {
  const char* text = "";
  switch (props_mode & TD_WINDOW) {
  case TD_WINDOW_MINIMUM:
    text = "MINIMUM";
    data = TD_WINDOW_NORMAL;
    break;
  case TD_WINDOW_NORMAL:
    text = "NORMAL";
    data = TD_WINDOW_MAXIMUM;
    break;
  case TD_WINDOW_MAXIMUM:
    text = "MAXIMUM";
    data = TD_WINDOW_MINIMUM;
    break;
  }
  if (b) {
    b->p1.text = text;
    return;
  }
  props_mode = (props_mode & ~TD_WINDOW) | data;
}

static UI_FUNCTION_ADV_CALLBACK(menu_transform_acb) {
  (void)data;
  if (b) {
    if (props_mode & DOMAIN_TIME)
      b->icon = BUTTON_ICON_CHECK;
    b->p1.text = (props_mode & DOMAIN_TIME) ? "ON" : "OFF";
    return;
  }
  props_mode ^= DOMAIN_TIME;
  select_lever_mode(LM_MARKER);
  request_to_redraw(REDRAW_FREQUENCY | REDRAW_AREA);
}

static UI_FUNCTION_ADV_CALLBACK(menu_transform_filter_acb) {
  if (b) {
    b->icon = (props_mode & TD_FUNC) == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    return;
  }
  props_mode = (props_mode & ~TD_FUNC) | data;
}

const menuitem_t menu_bandwidth[];
static UI_FUNCTION_ADV_CALLBACK(menu_bandwidth_sel_acb) {
  (void)data;
  if (b) {
    b->p1.u = get_bandwidth_frequency(config._bandwidth);
    return;
  }
  menu_push_submenu(menu_bandwidth);
}

static UI_FUNCTION_ADV_CALLBACK(menu_bandwidth_acb) {
  if (b) {
    b->icon = config._bandwidth == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = get_bandwidth_frequency(data);
    return;
  }
  set_bandwidth(data);
}

typedef struct {
  const char* text;
  uint16_t update_flag;
} __attribute__((packed)) vna_mode_data_t;

const vna_mode_data_t vna_mode_data[] = {
    //                        text (if 0 use checkbox) Redraw flags on change
    [VNA_MODE_AUTO_NAME] = {0, REDRAW_BACKUP},
#ifdef __USE_SMOOTH__
    [VNA_MODE_SMOOTH] = {"Geom\0Arith", REDRAW_BACKUP},
#endif
#ifdef __USE_SERIAL_CONSOLE__
    [VNA_MODE_CONNECTION] = {"USB\0SERIAL", REDRAW_BACKUP},
#endif
    [VNA_MODE_SEARCH] = {"MAXIMUM\0MINIMUM", REDRAW_BACKUP},
    [VNA_MODE_SHOW_GRID] = {0, REDRAW_BACKUP | REDRAW_AREA},
    [VNA_MODE_DOT_GRID] = {0, REDRAW_BACKUP | REDRAW_AREA},
#ifdef __USE_BACKUP__
    [VNA_MODE_BACKUP] = {0, REDRAW_BACKUP},
#endif
#ifdef __FLIP_DISPLAY__
    [VNA_MODE_FLIP_DISPLAY] = {0, REDRAW_BACKUP | REDRAW_ALL},
#endif
#ifdef __DIGIT_SEPARATOR__
    [VNA_MODE_SEPARATOR] = {"DOT '.'\0COMMA ','", REDRAW_BACKUP | REDRAW_MARKER | REDRAW_FREQUENCY},
#endif
#ifdef __SD_CARD_DUMP_TIFF__
    [VNA_MODE_TIFF] = {"BMP\0TIF", REDRAW_BACKUP},
#endif
#ifdef __USB_UID__
    [VNA_MODE_USB_UID] = {0, REDRAW_BACKUP},
#endif
};

void apply_vna_mode(uint16_t idx, vna_mode_ops operation) {
  uint16_t m = 1 << idx;
  uint16_t old = config._vna_mode;
  if (operation == VNA_MODE_CLR)
    config._vna_mode &= ~m; // clear
  else if (operation == VNA_MODE_SET)
    config._vna_mode |= m; // set
  else
    config._vna_mode ^= m; // toggle
  if (old == config._vna_mode)
    return;
  request_to_redraw(vna_mode_data[idx].update_flag);
  config_service_notify_configuration_changed();
  // Custom processing after apply
  switch (idx) {
#ifdef __USE_SERIAL_CONSOLE__
  case VNA_MODE_CONNECTION:
    shell_reset_console();
    break;
#endif
  case VNA_MODE_SEARCH:
    marker_search();
#ifdef UI_USE_LEVELER_SEARCH_MODE
    select_lever_mode(LM_SEARCH);
#endif
    break;
#ifdef __FLIP_DISPLAY__
  case VNA_MODE_FLIP_DISPLAY:
    lcd_set_flip(VNA_MODE(VNA_MODE_FLIP_DISPLAY));
    draw_all();
    break;
#endif
  }
}

static UI_FUNCTION_ADV_CALLBACK(menu_vna_mode_acb) {
  if (b) {
    const char* t = vna_mode_data[data].text;
    if (t == 0)
      b->icon = VNA_MODE(data) ? BUTTON_ICON_CHECK : BUTTON_ICON_NOCHECK;
    else
      b->p1.text = VNA_MODE(data) ? t + strlen(t) + 1 : t;
    return;
  }
  apply_vna_mode(data, VNA_MODE_TOGGLE);
}

#ifdef __USE_SMOOTH__
static UI_FUNCTION_ADV_CALLBACK(menu_smooth_acb) {
  if (b) {
    b->icon = get_smooth_factor() == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = data;
    return;
  }
  set_smooth_factor(data);
}
#endif

const menuitem_t menu_sweep_points[];
static UI_FUNCTION_ADV_CALLBACK(menu_points_sel_acb) {
  (void)data;
  if (b) {
    b->p1.u = sweep_points;
    return;
  }
  menu_push_submenu(menu_sweep_points);
}

static const uint16_t point_counts_set[POINTS_SET_COUNT] = POINTS_SET;
static UI_FUNCTION_ADV_CALLBACK(menu_points_acb) {
  uint16_t p_count = point_counts_set[data];
  if (b) {
    b->icon = sweep_points == p_count ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = p_count;
    return;
  }
  set_sweep_points(p_count);
}

const menuitem_t menu_power[];
static UI_FUNCTION_ADV_CALLBACK(menu_power_sel_acb) {
  (void)data;
  if (b) {
    if (current_props._power != SI5351_CLK_DRIVE_STRENGTH_AUTO)
      plot_printf(b->label, sizeof(b->label), "POWER" R_LINK_COLOR "  %um" S_AMPER,
                  2 + current_props._power * 2);
    return;
  }
  menu_push_submenu(menu_power);
}

static UI_FUNCTION_ADV_CALLBACK(menu_power_acb) {
  if (b) {
    b->icon = current_props._power == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = 2 + data * 2;
    return;
  }
  set_power(data);
}

// Process keyboard button callback, and run keyboard function
static void ui_keyboard_cb(uint16_t data, button_t* b);
static UI_FUNCTION_ADV_CALLBACK(menu_keyboard_acb) {
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
static UI_FUNCTION_ADV_CALLBACK(menu_scale_keyboard_acb) {
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
static UI_FUNCTION_CALLBACK(menu_auto_scale_cb) {
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

static UI_FUNCTION_ADV_CALLBACK(menu_pause_acb) {
  (void)data;
  if (b) {
    b->p1.text = (sweep_mode & SWEEP_ENABLE) ? "PAUSE" : "RESUME";
    b->icon = (sweep_mode & SWEEP_ENABLE) ? BUTTON_ICON_NOCHECK : BUTTON_ICON_CHECK;
    return;
  }
  toggle_sweep();
}

#define UI_MARKER_EDELAY 6
static UI_FUNCTION_CALLBACK(menu_marker_op_cb) {
  freq_t freq = get_marker_frequency(active_marker);
  if (freq == 0)
    return; // no active marker
  switch (data) {
  case ST_START:
  case ST_STOP:
  case ST_CENTER:
    set_sweep_frequency(data, freq);
    break;
  case ST_SPAN:
    if (previous_marker == MARKER_INVALID || active_marker == previous_marker) {
      // if only 1 marker is active, keep center freq and make span the marker comes to the edge
      freq_t center = get_sweep_frequency(ST_CENTER);
      freq_t span = center > freq ? center - freq : freq - center;
      set_sweep_frequency(ST_SPAN, span * 2);
    } else {
      // if 2 or more marker active, set start and stop freq to each marker
      freq_t freq2 = get_marker_frequency(previous_marker);
      if (freq2 == 0)
        return;
      if (freq > freq2)
        SWAP(freq_t, freq2, freq);
      set_sweep_frequency(ST_START, freq);
      set_sweep_frequency(ST_STOP, freq2);
    }
    break;
  case UI_MARKER_EDELAY:
    if (current_trace != TRACE_INVALID) {
      int ch = trace[current_trace].channel;
      float (*array)[2] = measured[ch];
      int index = markers[active_marker].index;
      float v = groupdelay_from_array(index, array[index]);
      set_electrical_delay(ch, current_props._electrical_delay[ch] + v);
    }
    break;
  }
  ui_mode_normal();
}

static UI_FUNCTION_CALLBACK(menu_marker_search_dir_cb) {
  marker_search_dir(markers[active_marker].index,
                    data == MK_SEARCH_RIGHT ? MK_SEARCH_RIGHT : MK_SEARCH_LEFT);
  props_mode &= ~TD_MARKER_TRACK;
#ifdef UI_USE_LEVELER_SEARCH_MODE
  select_lever_mode(LM_SEARCH);
#endif
}

static UI_FUNCTION_ADV_CALLBACK(menu_marker_tracking_acb) {
  (void)data;
  if (b) {
    b->icon = (props_mode & TD_MARKER_TRACK) ? BUTTON_ICON_CHECK : BUTTON_ICON_NOCHECK;
    return;
  }
  props_mode ^= TD_MARKER_TRACK;
}

#ifdef __VNA_MEASURE_MODULE__
extern const menuitem_t* menu_measure_list[];
static UI_FUNCTION_ADV_CALLBACK(menu_measure_acb) {
  if (b) {
    b->icon = current_props._measure == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    return;
  }
  plot_set_measure_mode(data);
  menu_set_submenu(menu_measure_list[current_props._measure]);
}

static UI_FUNCTION_CALLBACK(menu_measure_cb) {
  (void)data;
  menu_push_submenu(menu_measure_list[current_props._measure]);
}
#endif

static void active_marker_check(void) {
  int i;
  // Auto select active marker if disabled
  if (active_marker == MARKER_INVALID)
    for (i = 0; i < MARKERS_MAX; i++)
      if (markers[i].enabled)
        active_marker = i;
  // Auto select previous marker if disabled
  if (previous_marker == active_marker)
    previous_marker = MARKER_INVALID;
  if (previous_marker == MARKER_INVALID) {
    for (i = 0; i < MARKERS_MAX; i++)
      if (markers[i].enabled && i != active_marker)
        previous_marker = i;
  }
}

static UI_FUNCTION_ADV_CALLBACK(menu_marker_sel_acb) {
  // if (data >= MARKERS_MAX) return;
  int mk = data;
  if (b) {
    if (mk == active_marker)
      b->icon = BUTTON_ICON_CHECK_AUTO;
    else if (markers[mk].enabled)
      b->icon = BUTTON_ICON_CHECK;
    b->p1.u = mk + 1;
    return;
  }
  // Marker select click
  if (markers[mk].enabled) {          // Marker enabled
    if (mk == active_marker) {        // If active marker:
      markers[mk].enabled = FALSE;    //  disable it
      mk = previous_marker;           //  set select from previous marker
      active_marker = MARKER_INVALID; //  invalidate active
      request_to_redraw(REDRAW_AREA);
    }
  } else {
    markers[mk].enabled = TRUE; // Enable marker
  }
  previous_marker = active_marker; // set previous marker as current active
  active_marker = mk;              // set new active marker
  active_marker_check();
  request_to_redraw(REDRAW_MARKER);
}

static UI_FUNCTION_CALLBACK(menu_marker_disable_all_cb) {
  (void)data;
  for (int i = 0; i < MARKERS_MAX; i++)
    markers[i].enabled = FALSE; // all off
  previous_marker = MARKER_INVALID;
  active_marker = MARKER_INVALID;
  request_to_redraw(REDRAW_AREA);
}

static UI_FUNCTION_ADV_CALLBACK(menu_marker_delta_acb) {
  (void)data;
  if (b) {
    b->icon = props_mode & TD_MARKER_DELTA ? BUTTON_ICON_CHECK : BUTTON_ICON_NOCHECK;
    return;
  }
  props_mode ^= TD_MARKER_DELTA;
  request_to_redraw(REDRAW_MARKER);
}

#ifdef __USE_SERIAL_CONSOLE__
static UI_FUNCTION_ADV_CALLBACK(menu_serial_speed_acb) {
  static const uint32_t usart_speed[] = {19200,  38400,  57600,   115200,  230400,
                                         460800, 921600, 1843200, 2000000, 3000000};
  uint32_t speed = usart_speed[data];
  if (b) {
    b->icon = config._serial_speed == speed ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = speed;
    return;
  }
  shell_update_speed(speed);
}

extern const menuitem_t menu_serial_speed[];
static UI_FUNCTION_ADV_CALLBACK(menu_serial_speed_sel_acb) {
  (void)data;
  if (b) {
    b->p1.u = config._serial_speed;
    return;
  }
  menu_push_submenu(menu_serial_speed);
}
#endif

#ifdef USE_VARIABLE_OFFSET_MENU
static UI_FUNCTION_ADV_CALLBACK(menu_offset_acb) {
  int32_t offset = (data + 1) * FREQUENCY_OFFSET_STEP;
  if (b) {
    b->icon = IF_OFFSET == offset ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = offset;
    return;
  }
  si5351_set_frequency_offset(offset);
}

const menuitem_t menu_offset[];
static UI_FUNCTION_ADV_CALLBACK(menu_offset_sel_acb) {
  (void)data;
  if (b) {
    b->p1.i = IF_OFFSET;
    return;
  }
  menu_push_submenu(menu_offset);
}
#endif

#ifdef __LCD_BRIGHTNESS__
// Brightness control range 0 - 100
void lcd_set_brightness(uint16_t b) {
  dac_setvalue_ch2(700 + b * (4000 - 700) / 100);
}

static UI_FUNCTION_ADV_CALLBACK(menu_brightness_acb) {
  (void)data;
  if (b) {
    b->p1.u = config._brightness;
    return;
  }
  int16_t value = config._brightness;
  lcd_set_colors(LCD_MENU_TEXT_COLOR, LCD_MENU_COLOR);
  lcd_fill(LCD_WIDTH / 2 - FONT_STR_WIDTH(12), LCD_HEIGHT / 2 - 20, FONT_STR_WIDTH(23), 40);
  lcd_printf(LCD_WIDTH / 2 - FONT_STR_WIDTH(8), LCD_HEIGHT / 2 - 13, "BRIGHTNESS %3d%% ", value);
  lcd_printf(LCD_WIDTH / 2 - FONT_STR_WIDTH(11), LCD_HEIGHT / 2 + 2,
             S_LARROW " USE LEVELER BUTTON " S_RARROW);
  while (TRUE) {
    uint16_t status = ui_input_check();
    if (status & (EVT_UP | EVT_DOWN)) {
      do {
        if (status & EVT_UP)
          value += 5;
        if (status & EVT_DOWN)
          value -= 5;
        if (value < 0)
          value = 0;
        if (value > 100)
          value = 100;
        lcd_printf(LCD_WIDTH / 2 - FONT_STR_WIDTH(8), LCD_HEIGHT / 2 - 13, "BRIGHTNESS %3d%% ",
                   value);
        lcd_set_brightness(value);
        chThdSleepMilliseconds(200);
      } while ((status = ui_input_wait_release()) & (EVT_DOWN | EVT_UP));
    }
    if (status == EVT_BUTTON_SINGLE_CLICK)
      break;
  }
  config._brightness = (uint8_t)value;
  request_to_redraw(REDRAW_BACKUP | REDRAW_AREA);
  config_service_notify_configuration_changed();
  ui_mode_normal();
}
#endif

//=====================================================================================================
//                                 SD card save / load functions
//=====================================================================================================
#ifdef __USE_SD_CARD__
bool sd_temp_buffer_acquire(size_t required_bytes, sd_temp_buffer_t* handle) {
  if (handle == NULL) {
    return false;
  }
  handle->data = NULL;
  handle->size = 0;
  handle->using_measurement = false;
  sweep_workspace_t workspace;
  if (required_bytes <= sizeof measured[1] &&
      sweep_service_workspace_acquire(&workspace) && workspace.size >= required_bytes) {
    handle->data = workspace.buffer;
    handle->size = workspace.size;
    handle->using_measurement = true;
    return true;
  }
  if (sizeof(spi_buffer) < required_bytes) {
    return false;
  }
  handle->data = (uint8_t*)spi_buffer;
  handle->size = sizeof(spi_buffer);
  handle->using_measurement = false;
  return true;
}

void sd_temp_buffer_release(sd_temp_buffer_t* handle) {
  if (handle != NULL && handle->using_measurement) {
    sweep_service_workspace_release();
    handle->using_measurement = false;
  }
}

//=====================================================================================================
// S1P and S2P file headers, and data structures
//=====================================================================================================
static const char s1_file_header[] = "!File created by NanoVNA\r\n"
                                     "# Hz S RI R 50\r\n";

static const char s1_file_param[] = "%u % f % f\r\n";

static const char s2_file_header[] = "!File created by NanoVNA\r\n"
                                     "# Hz S RI R 50\r\n";

static const char s2_file_param[] = "%u % f % f % f % f 0 0 0 0\r\n";

static FILE_SAVE_CALLBACK(save_snp) {
  const char* s_file_format;
  char* buf_8 = (char*)spi_buffer;
  FRESULT res;
  UINT size;
  if (format == FMT_S1P_FILE) {
    s_file_format = s1_file_param;
    res = f_write(f, s1_file_header, sizeof(s1_file_header) - 1, &size);
  } else {
    s_file_format = s2_file_param;
    res = f_write(f, s2_file_header, sizeof(s2_file_header) - 1, &size);
  }
  for (int i = 0; i < sweep_points && res == FR_OK; i++) {
    size = plot_printf(buf_8, 128, s_file_format, get_frequency(i), measured[0][i][0],
                       measured[0][i][1], measured[1][i][0], measured[1][i][1]);
    res = f_write(f, buf_8, size, &size);
  }
  return res;
}


static const uint8_t bmp_header_v4[BMP_H1_SIZE + BMP_V4_SIZE] = {
    0x42, 0x4D, BMP_UINT32(BMP_FILE_SIZE), BMP_UINT16(0), BMP_UINT16(0), BMP_UINT32(BMP_HEAD_SIZE),
    BMP_UINT32(BMP_V4_SIZE), BMP_UINT32(LCD_WIDTH), BMP_UINT32(LCD_HEIGHT), BMP_UINT16(1),
    BMP_UINT16(16), BMP_UINT32(3), BMP_UINT32(BMP_SIZE), BMP_UINT32(0x0EC4), BMP_UINT32(0x0EC4),
    BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0b1111100000000000), BMP_UINT32(0b0000011111100000),
    BMP_UINT32(0b0000000000011111), BMP_UINT32(0b0000000000000000), 'B', 'G', 'R', 's',
    BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0),
    BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0)};

static FILE_SAVE_CALLBACK(save_bmp) {
  (void)format;
  UINT size;
  sd_temp_buffer_t workspace;
  size_t required = LCD_WIDTH * sizeof(uint16_t);
  if (!sd_temp_buffer_acquire(required, &workspace)) {
    return FR_NOT_ENOUGH_CORE;
  }
  uint16_t* buf_16 = (uint16_t*)workspace.data;
  FRESULT res = f_write(f, bmp_header_v4, sizeof(bmp_header_v4), &size);
  lcd_set_background(LCD_SWEEP_LINE_COLOR);
  for (int y = LCD_HEIGHT - 1; y >= 0 && res == FR_OK; y--) {
    lcd_read_memory(0, y, LCD_WIDTH, 1, buf_16);
    swap_bytes(buf_16, LCD_WIDTH);
    res = f_write(f, buf_16, LCD_WIDTH * sizeof(uint16_t), &size);
    lcd_fill(LCD_WIDTH - 1, y, 1, 1);
  }
  sd_temp_buffer_release(&workspace);
  return res;
}

#ifdef __SD_CARD_DUMP_TIFF__
static const uint8_t tif_header[] = {
    0x49, 0x49, BMP_UINT16(0x002A), BMP_UINT32(0x0008), BMP_UINT16(IFD_ENTRIES_COUNT),
    IFD_ENTRY(256, IFD_LONG, 1, LCD_WIDTH), IFD_ENTRY(257, IFD_LONG, 1, LCD_HEIGHT),
    IFD_ENTRY(258, IFD_SHORT, 3, IFD_BPS_OFFSET), IFD_ENTRY(259, IFD_SHORT, 1, TIFF_PACKBITS),
    IFD_ENTRY(262, IFD_SHORT, 1, TIFF_PHOTOMETRIC_RGB), IFD_ENTRY(273, IFD_LONG, 1, IFD_STRIP_OFFSET),
    IFD_ENTRY(277, IFD_SHORT, 1, 3), BMP_UINT32(0)};

static FILE_SAVE_CALLBACK(save_tiff) {
  (void)format;
  UINT size;
  size_t raw_required = LCD_WIDTH * sizeof(uint16_t);
  size_t packed_required = 128 + LCD_WIDTH * 3;
  size_t required = raw_required > packed_required ? raw_required : packed_required;
  sd_temp_buffer_t workspace;
  if (!sd_temp_buffer_acquire(required, &workspace)) {
    return FR_NOT_ENOUGH_CORE;
  }
  uint16_t* buf_16 = (uint16_t*)workspace.data;
  char* buf_8;
  FRESULT res = f_write(f, tif_header, sizeof(tif_header), &size);
  lcd_set_background(LCD_SWEEP_LINE_COLOR);
  for (int y = 0; y < LCD_HEIGHT && res == FR_OK; y++) {
    buf_8 = (char*)buf_16 + 128;
    lcd_read_memory(0, y, LCD_WIDTH, 1, buf_16);
    for (int x = LCD_WIDTH - 1; x >= 0; x--) {
      uint16_t color = (buf_16[x] << 8) | (buf_16[x] >> 8);
      buf_8[3 * x + 0] = (color >> 8) & 0xF8;
      buf_8[3 * x + 1] = (color >> 3) & 0xFC;
      buf_8[3 * x + 2] = (color << 3) & 0xF8;
    }
    size = packbits(buf_8, (char*)buf_16, LCD_WIDTH * 3);
    res = f_write(f, buf_16, size, &size);
    lcd_fill(LCD_WIDTH - 1, y, 1, 1);
  }
  sd_temp_buffer_release(&workspace);
  return res;
}

#endif


static FILE_SAVE_CALLBACK(save_cal) {
  (void)format;
  UINT size;
  const char* src = (char*)&current_props;
  const uint32_t total = sizeof(current_props);
  return f_write(f, src, total, &size);
}


#ifdef __SD_CARD_DUMP_FIRMWARE__
static FILE_SAVE_CALLBACK(save_bin) {
  (void)format;
  UINT size;
  const char* src = (const char*)FLASH_START_ADDRESS;
  const uint32_t total = FLASH_TOTAL_SIZE;
  return f_write(f, src, total, &size);
}
#endif

#endif

#if defined(__USE_SD_CARD__) && FF_USE_MKFS
_Static_assert(sizeof(spi_buffer) >= FF_MAX_SS, "spi_buffer is too small for mkfs work buffer");

static FRESULT sd_card_format(void) {
  sd_temp_buffer_t workspace;
  if (!sd_temp_buffer_acquire(FF_MAX_SS, &workspace)) {
    return FR_NOT_ENOUGH_CORE;
  }
  BYTE* work = workspace.data;
  FATFS* fs = filesystem_volume();
  f_mount(NULL, "", 0);
  DSTATUS status = disk_initialize(0);
  if (status & STA_NOINIT) {
    sd_temp_buffer_release(&workspace);
    return FR_NOT_READY;
  }
  /* Allow mkfs to pick FAT12/16 for small cards and FAT32 for larger media. */
  MKFS_PARM opt = {.fmt = FM_FAT | FM_FAT32, .n_fat = 1, .align = 0, .n_root = 0, .au_size = 0};
  FRESULT res = f_mkfs("", &opt, work, FF_MAX_SS);
  if (res != FR_OK) {
    sd_temp_buffer_release(&workspace);
    return res;
  }
  disk_ioctl(0, CTRL_SYNC, NULL);
  memset(fs, 0, sizeof(*fs));
  FRESULT mount_status = f_mount(fs, "", 1);
  sd_temp_buffer_release(&workspace);
  return mount_status;
}

static UI_FUNCTION_CALLBACK(menu_sdcard_format_cb) {
  (void)data;
  bool resume = (sweep_mode & SWEEP_ENABLE) != 0;
  if (resume)
    toggle_sweep();
  systime_t start = chVTGetSystemTimeX();
  ui_message_box_draw("FORMAT SD", "Formatting...");
  chThdSleepMilliseconds(120);
  FRESULT result = sd_card_format();
  if (resume)
    toggle_sweep();
  char* msg = (char*)spi_buffer;
  FRESULT res = result;
  if (res == FR_OK) {
    uint32_t elapsed_ms = (uint32_t)ST2MS(chVTTimeElapsedSinceX(start));
    plot_printf(msg, 32, "OK %lums", (unsigned long)elapsed_ms);
  } else {
    plot_printf(msg, 32, "ERR %d", res);
  }
  ui_message_box("FORMAT SD", msg, 2000);
  ui_mode_normal();
}
#endif

#if SD_BROWSER_ENABLED
#define FILE_LOAD_ENTRY(handler) (handler)
#else
#define FILE_LOAD_ENTRY(handler) NULL
#endif

#define FILE_OPTIONS(ext, save_cb, load_cb, options)                                                    \
  { ext, save_cb, FILE_LOAD_ENTRY(load_cb), options }

const sd_file_format_t file_opt[] = {
    [FMT_S1P_FILE] = FILE_OPTIONS("s1p", save_snp, load_snp, 0),
    [FMT_S2P_FILE] = FILE_OPTIONS("s2p", save_snp, load_snp, 0),
    [FMT_BMP_FILE] = FILE_OPTIONS("bmp", save_bmp, load_bmp, FILE_OPT_REDRAW | FILE_OPT_CONTINUE),
#ifdef __SD_CARD_DUMP_TIFF__
    [FMT_TIF_FILE] = FILE_OPTIONS("tif", save_tiff, load_tiff, FILE_OPT_REDRAW | FILE_OPT_CONTINUE),
#endif
    [FMT_CAL_FILE] = FILE_OPTIONS("cal", save_cal, load_cal, 0),
#ifdef __SD_CARD_DUMP_FIRMWARE__
    [FMT_BIN_FILE] = FILE_OPTIONS("bin", save_bin, NULL, 0),
#endif
#ifdef __SD_CARD_LOAD__
    [FMT_CMD_FILE] = FILE_OPTIONS("cmd", NULL, load_cmd, 0),
#endif
};

static FRESULT ui_create_file(char* fs_filename) {
  FRESULT res = f_mount(filesystem_volume(), "", 1);
  if (res != FR_OK)
    return res;
  FIL* const file = filesystem_file();
  res = f_open(file, fs_filename, FA_CREATE_ALWAYS | FA_READ | FA_WRITE);
  return res;
}

static char* ui_format_filename(char* buffer, size_t length, const char* name, uint8_t format) {
#if FF_USE_LFN >= 1
  if (name == NULL) {
    uint32_t tr = rtc_get_tr_bcd();
    uint32_t dr = rtc_get_dr_bcd();
    plot_printf(buffer, length, "VNA_%06x_%06x.%s", dr, tr, file_opt[format].ext);
  } else {
    plot_printf(buffer, length, "%s.%s", name, file_opt[format].ext);
  }
#else
  if (name == NULL) {
    plot_printf(buffer, length, "%08x.%s", rtc_get_fat(), file_opt[format].ext);
  } else {
    plot_printf(buffer, length, "%s.%s", name, file_opt[format].ext);
  }
#endif
  return buffer;
}

static void ui_save_file(char* name, uint8_t format) {
  char* fs_filename = (char*)spi_buffer;
  file_save_cb_t save = file_opt[format].save;
  if (save == NULL)
    return;
  if (ui_mode != UI_NORMAL && (file_opt[format].opt & FILE_OPT_REDRAW)) {
    ui_mode_normal();
    draw_all();
  }

  ui_format_filename(fs_filename, FF_LFN_BUF, name, format);

  FRESULT res = ui_create_file(fs_filename);
  if (res == FR_OK) {
    FIL* const file = filesystem_file();
    res = save(file, format);
    f_close(file);
  }
  if (keyboard_temp == 1)
    toggle_sweep();
  if (res == FR_OK) {
    ui_format_filename(fs_filename, FF_LFN_BUF, name, format);
  }
  ui_message_box("SD CARD SAVE", res == FR_OK ? fs_filename : "  Fail write  ", 2000);
  request_to_redraw(REDRAW_AREA | REDRAW_FREQUENCY);
  ui_mode_normal();
}

uint16_t fix_screenshot_format(uint16_t data) {
#ifdef __SD_CARD_DUMP_TIFF__
  if (data == FMT_BMP_FILE && VNA_MODE(VNA_MODE_TIFF))
    return FMT_TIF_FILE;
#endif
  return data;
}


static UI_FUNCTION_CALLBACK(menu_sdcard_cb) {
  keyboard_temp = (sweep_mode & SWEEP_ENABLE) ? 1 : 0;
  if (keyboard_temp)
    toggle_sweep();
  data = fix_screenshot_format(data);
  if (VNA_MODE(VNA_MODE_AUTO_NAME))
    ui_save_file(NULL, data);
  else
    ui_mode_keypad(data + KM_S1P_NAME);
}

static UI_FUNCTION_ADV_CALLBACK(menu_band_sel_acb) {
  (void)data;
  static const char* gen_names[] = {"Si5351", "MS5351", "SWC5351"};
  if (b) {
    b->p1.text = gen_names[config._band_mode];
    return;
  }
  if (++config._band_mode >= ARRAY_COUNT(gen_names))
    config._band_mode = 0;
  si5351_set_band_mode(config._band_mode);
  config_service_notify_configuration_changed();
}

#if STORED_TRACES > 0
static UI_FUNCTION_ADV_CALLBACK(menu_stored_trace_acb) {
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
static UI_FUNCTION_CALLBACK(menu_back_cb) {
  (void)data;
  menu_move_back(false);
}

// Back button submenu list
static const menuitem_t menu_back[] = {
    {MT_CALLBACK, 0, S_LARROW " BACK", menu_back_cb}, {MT_NEXT, 0, NULL, NULL} // sentinel
};

#if SD_BROWSER_ENABLED
const menuitem_t menu_sdcard_browse[] = {
    {MT_CALLBACK, FMT_BMP_FILE, "LOAD\nSCREENSHOT", menu_sdcard_browse_cb},
    {MT_CALLBACK, FMT_S1P_FILE, "LOAD S1P", menu_sdcard_browse_cb},
    {MT_CALLBACK, FMT_S2P_FILE, "LOAD S2P", menu_sdcard_browse_cb},
    {MT_CALLBACK, FMT_CAL_FILE, "LOAD CAL", menu_sdcard_browse_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif


static const menuitem_t menu_sdcard[] = {
#if SD_BROWSER_ENABLED
    {MT_SUBMENU, 0, "LOAD", menu_sdcard_browse},
#endif
    {MT_CALLBACK, FMT_S1P_FILE, "SAVE S1P", menu_sdcard_cb},
    {MT_CALLBACK, FMT_S2P_FILE, "SAVE S2P", menu_sdcard_cb},
    {MT_CALLBACK, FMT_BMP_FILE, "SCREENSHOT", menu_sdcard_cb},
    {MT_CALLBACK, FMT_CAL_FILE, "SAVE\nCALIBRATION", menu_sdcard_cb},
#if FF_USE_MKFS
    {MT_CALLBACK, 0, "FORMAT SD", menu_sdcard_format_cb},
#endif
    {MT_ADV_CALLBACK, VNA_MODE_AUTO_NAME, "AUTO NAME", menu_vna_mode_acb},
#ifdef __SD_CARD_DUMP_TIFF__
    {MT_ADV_CALLBACK, VNA_MODE_TIFF, "IMAGE FORMAT\n " R_LINK_COLOR "%s", menu_vna_mode_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

static const menuitem_t menu_calop[] = {
    {MT_ADV_CALLBACK, CAL_OPEN, "OPEN", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_SHORT, "SHORT", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_LOAD, "LOAD", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_ISOLN, "ISOLN", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_THRU, "THRU", menu_calop_acb},
    //{ MT_ADV_CALLBACK, KM_EDELAY, "E-DELAY", menu_keyboard_acb },
    {MT_CALLBACK, 0, "DONE", menu_caldone_cb},
    {MT_CALLBACK, 1, "DONE IN RAM", menu_caldone_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_save[] = {
#if SD_BROWSER_ENABLED
    {MT_CALLBACK, FMT_CAL_FILE, "SAVE TO\n SD CARD", menu_sdcard_cb},
#endif
    {MT_ADV_CALLBACK, 0, "Empty %d", menu_save_acb},
    {MT_ADV_CALLBACK, 1, "Empty %d", menu_save_acb},
    {MT_ADV_CALLBACK, 2, "Empty %d", menu_save_acb},
#if SAVEAREA_MAX > 3
    {MT_ADV_CALLBACK, 3, "Empty %d", menu_save_acb},
#endif
#if SAVEAREA_MAX > 4
    {MT_ADV_CALLBACK, 4, "Empty %d", menu_save_acb},
#endif
#if SAVEAREA_MAX > 5
    {MT_ADV_CALLBACK, 5, "Empty %d", menu_save_acb},
#endif
#if SAVEAREA_MAX > 6
    {MT_ADV_CALLBACK, 6, "Empty %d", menu_save_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_recall[] = {
#if SD_BROWSER_ENABLED
    {MT_CALLBACK, FMT_CAL_FILE, "LOAD FROM\n SD CARD", menu_sdcard_browse_cb},
#endif
    {MT_ADV_CALLBACK, 0, "Empty %d", menu_recall_acb},
    {MT_ADV_CALLBACK, 1, "Empty %d", menu_recall_acb},
    {MT_ADV_CALLBACK, 2, "Empty %d", menu_recall_acb},
#if SAVEAREA_MAX > 3
    {MT_ADV_CALLBACK, 3, "Empty %d", menu_recall_acb},
#endif
#if SAVEAREA_MAX > 4
    {MT_ADV_CALLBACK, 4, "Empty %d", menu_recall_acb},
#endif
#if SAVEAREA_MAX > 5
    {MT_ADV_CALLBACK, 5, "Empty %d", menu_recall_acb},
#endif
#if SAVEAREA_MAX > 6
    {MT_ADV_CALLBACK, 6, "Empty %d", menu_recall_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_power[] = {
    {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_AUTO, "AUTO", menu_power_acb},
    {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_2MA, "%u m" S_AMPER, menu_power_acb},
    {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_4MA, "%u m" S_AMPER, menu_power_acb},
    {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_6MA, "%u m" S_AMPER, menu_power_acb},
    {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_8MA, "%u m" S_AMPER, menu_power_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_cal_flow[] = {
    {MT_SUBMENU, 0, "MECH CAL", menu_calop},
    {MT_ADV_CALLBACK, 0, "CAL RANGE", menu_cal_range_acb},
    {MT_ADV_CALLBACK, 0, "CAL POWER", menu_power_sel_acb},
    {MT_SUBMENU, 0, "SAVE CAL", menu_save},
    {MT_ADV_CALLBACK, 0, "CAL APPLY", menu_cal_apply_acb},
    {MT_ADV_CALLBACK, 0, "ENHANCED\nRESPONSE", menu_cal_enh_acb},
#ifdef __VNA_Z_RENORMALIZATION__
    {MT_ADV_CALLBACK, KM_CAL_LOAD_R, "LOAD STD\n " R_LINK_COLOR "%bF" S_OHM, menu_keyboard_acb},
#endif
    {MT_CALLBACK, 0, "CAL RESET", menu_cal_reset_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_state_io[] = {
    {MT_SUBMENU, 0, "SAVE CAL", menu_save},
    {MT_SUBMENU, 0, "RECALL CAL", menu_recall},
    {MT_ADV_CALLBACK, 0, "CAL APPLY", menu_cal_apply_acb},
    {MT_CALLBACK, 0, "CAL RESET", menu_cal_reset_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_cal_menu[] = {
    {MT_SUBMENU, 0, "MECH CAL", menu_cal_flow},
    {MT_SUBMENU, 0, "SAVE/RECALL", menu_state_io},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_trace[] = {
    {MT_ADV_CALLBACK, 0, "TRACE %d", menu_trace_acb},
    {MT_ADV_CALLBACK, 1, "TRACE %d", menu_trace_acb},
    {MT_ADV_CALLBACK, 2, "TRACE %d", menu_trace_acb},
    {MT_ADV_CALLBACK, 3, "TRACE %d", menu_trace_acb},
#if STORED_TRACES == 1
    {MT_ADV_CALLBACK, 0, "%s TRACE", menu_stored_trace_acb},
#elif STORED_TRACES > 1
    {MT_ADV_CALLBACK, 0, "%s TRACE A", menu_stored_trace_acb},
    {MT_ADV_CALLBACK, 1, "%s TRACE B", menu_stored_trace_acb},
#if STORED_TRACES > 2
    {MT_ADV_CALLBACK, 2, "%s TRACE C", menu_stored_trace_acb},
#endif
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_format4[] = {
    {MT_ADV_CALLBACK, F_S21 | TRC_Rser, "SERIES R", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Xser, "SERIES X", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Zser, "SERIES |Z|", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Rsh, "SHUNT R", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Xsh, "SHUNT X", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Zsh, "SHUNT |Z|", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Qs21, "Q FACTOR", menu_format_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_formatS21[] = {
    {MT_ADV_CALLBACK, F_S21 | TRC_LOGMAG, "LOGMAG", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_PHASE, "PHASE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_DELAY, "DELAY", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_SMITH, "SMITH", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_POLAR, "POLAR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_LINEAR, "LINEAR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_REAL, "REAL", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_IMAG, "IMAG", menu_format_acb},
    {MT_SUBMENU, 0, S_RARROW " MORE", menu_format4},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_format3[] = {
    {MT_ADV_CALLBACK, F_S11 | TRC_ZPHASE, "Z PHASE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Cs, "SERIES C", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Ls, "SERIES L", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Rp, "PARALLEL R", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Xp, "PARALLEL X", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Cp, "PARALLEL C", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Lp, "PARALLEL L", menu_format_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_format2[] = {
    {MT_ADV_CALLBACK, F_S11 | TRC_POLAR, "POLAR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_LINEAR, "LINEAR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_REAL, "REAL", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_IMAG, "IMAG", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Q, "Q FACTOR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_G, "CONDUCTANCE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_B, "SUSCEPTANCE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Y, "|Y|", menu_format_acb},
    {MT_SUBMENU, 0, S_RARROW " MORE", menu_format3},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_formatS11[] = {
    {MT_ADV_CALLBACK, F_S11 | TRC_LOGMAG, "LOGMAG", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_PHASE, "PHASE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_DELAY, "DELAY", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_SMITH, "SMITH", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_SWR, "SWR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_R, "RESISTANCE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_X, "REACTANCE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Z, "|Z|", menu_format_acb},
    {MT_SUBMENU, 0, S_RARROW " MORE", menu_format2},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_scale[] = {
    {MT_CALLBACK, 0, "AUTO SCALE", menu_auto_scale_cb},
    {MT_ADV_CALLBACK, KM_TOP, "TOP", menu_scale_keyboard_acb},
    {MT_ADV_CALLBACK, KM_BOTTOM, "BOTTOM", menu_scale_keyboard_acb},
    {MT_ADV_CALLBACK, KM_SCALE, "SCALE/DIV", menu_scale_keyboard_acb},
    {MT_ADV_CALLBACK, KM_REFPOS, "REFERENCE\nPOSITION", menu_scale_keyboard_acb},
    {MT_ADV_CALLBACK, KM_EDELAY, "E-DELAY", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_S21OFFSET, "S21 OFFSET\n " R_LINK_COLOR "%b.3F" S_dB, menu_keyboard_acb},
#ifdef __USE_GRID_VALUES__
    {MT_ADV_CALLBACK, VNA_MODE_SHOW_GRID, "SHOW GRID\nVALUES", menu_vna_mode_acb},
    {MT_ADV_CALLBACK, VNA_MODE_DOT_GRID, "DOT GRID", menu_vna_mode_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
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

const menuitem_t menu_bandwidth[] = {
#ifdef BANDWIDTH_8000
    {MT_ADV_CALLBACK, BANDWIDTH_8000, "%u " S_Hz, menu_bandwidth_acb},
#endif
#ifdef BANDWIDTH_4000
    {MT_ADV_CALLBACK, BANDWIDTH_4000, "%u " S_Hz, menu_bandwidth_acb},
#endif
#ifdef BANDWIDTH_2000
    {MT_ADV_CALLBACK, BANDWIDTH_2000, "%u " S_Hz, menu_bandwidth_acb},
#endif
#ifdef BANDWIDTH_1000
    {MT_ADV_CALLBACK, BANDWIDTH_1000, "%u " S_Hz, menu_bandwidth_acb},
#endif
#ifdef BANDWIDTH_333
    {MT_ADV_CALLBACK, BANDWIDTH_333, "%u " S_Hz, menu_bandwidth_acb},
#endif
#ifdef BANDWIDTH_100
    {MT_ADV_CALLBACK, BANDWIDTH_100, "%u " S_Hz, menu_bandwidth_acb},
#endif
#ifdef BANDWIDTH_30
    {MT_ADV_CALLBACK, BANDWIDTH_30, "%u " S_Hz, menu_bandwidth_acb},
#endif
#ifdef BANDWIDTH_10
    {MT_ADV_CALLBACK, BANDWIDTH_10, "%u " S_Hz, menu_bandwidth_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

#ifdef __USE_SMOOTH__
const menuitem_t menu_smooth_count[] = {
    {MT_ADV_CALLBACK, VNA_MODE_SMOOTH, "SMOOTH\n " R_LINK_COLOR "%s avg", menu_vna_mode_acb},
    {MT_ADV_CALLBACK, 0, "SMOOTH\nOFF", menu_smooth_acb},
    {MT_ADV_CALLBACK, 1, "x%d", menu_smooth_acb},
    {MT_ADV_CALLBACK, 2, "x%d", menu_smooth_acb},
    {MT_ADV_CALLBACK, 4, "x%d", menu_smooth_acb},
    {MT_ADV_CALLBACK, 5, "x%d", menu_smooth_acb},
    {MT_ADV_CALLBACK, 6, "x%d", menu_smooth_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

const menuitem_t menu_display[] = {
    {MT_ADV_CALLBACK, 0, "TRACES", menu_traces_acb},
    {MT_SUBMENU, 0, "FORMAT\nS11", menu_formatS11},
    {MT_SUBMENU, 0, "FORMAT\nS21", menu_formatS21},
    {MT_ADV_CALLBACK, 0, "CHANNEL\n " R_LINK_COLOR "%s", menu_channel_acb},
    {MT_SUBMENU, 0, "SCALE", menu_scale},
    {MT_SUBMENU, 0, "MARKERS", menu_marker},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_measure_tools[] = {
    {MT_SUBMENU, 0, "TRANSFORM", menu_transform},
#ifdef __USE_SMOOTH__
    {MT_SUBMENU, 0, "DATA\nSMOOTH", menu_smooth_count},
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

const menuitem_t menu_sweep_points[] = {
    {MT_ADV_CALLBACK, KM_POINTS, "SET POINTS\n " R_LINK_COLOR "%d", (const void*)menu_keyboard_acb},
    {MT_ADV_CALLBACK, 0, "%d point", menu_points_acb},
#if POINTS_SET_COUNT > 1
    {MT_ADV_CALLBACK, 1, "%d point", menu_points_acb},
#endif
#if POINTS_SET_COUNT > 2
    {MT_ADV_CALLBACK, 2, "%d point", menu_points_acb},
#endif
#if POINTS_SET_COUNT > 3
    {MT_ADV_CALLBACK, 3, "%d point", menu_points_acb},
#endif
#if POINTS_SET_COUNT > 4
    {MT_ADV_CALLBACK, 4, "%d point", menu_points_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_stimulus[] = {
    {MT_ADV_CALLBACK, KM_START, "START", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_STOP, "STOP", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_CENTER, "CENTER", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_SPAN, "SPAN", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_CW, "CW FREQ", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_STEP, "FREQ STEP\n " R_LINK_COLOR "%bF" S_Hz, menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_VAR, "JOG STEP\n " R_LINK_COLOR "AUTO", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_POINTS, "SET POINTS\n " R_LINK_COLOR "%d", menu_keyboard_acb},
#if POINTS_SET_COUNT > 0
    {MT_ADV_CALLBACK, 0, "%d PTS", menu_points_acb},
#endif
#if POINTS_SET_COUNT > 1
    {MT_ADV_CALLBACK, 1, "%d PTS", menu_points_acb},
#endif
#if POINTS_SET_COUNT > 2
    {MT_ADV_CALLBACK, 2, "%d PTS", menu_points_acb},
#endif
#if POINTS_SET_COUNT > 3
    {MT_ADV_CALLBACK, 3, "%d PTS", menu_points_acb},
#endif
#if POINTS_SET_COUNT > 4
    {MT_ADV_CALLBACK, 4, "%d PTS", menu_points_acb},
#endif
    {MT_ADV_CALLBACK, 0, "MORE PTS\n " R_LINK_COLOR "%u", menu_points_sel_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_marker_sel[] = {
    {MT_ADV_CALLBACK, 0, "MARKER %d", menu_marker_sel_acb},
#if MARKERS_MAX >= 2
    {MT_ADV_CALLBACK, 1, "MARKER %d", menu_marker_sel_acb},
#endif
#if MARKERS_MAX >= 3
    {MT_ADV_CALLBACK, 2, "MARKER %d", menu_marker_sel_acb},
#endif
#if MARKERS_MAX >= 4
    {MT_ADV_CALLBACK, 3, "MARKER %d", menu_marker_sel_acb},
#endif
#if MARKERS_MAX >= 5
    {MT_ADV_CALLBACK, 4, "MARKER %d", menu_marker_sel_acb},
#endif
#if MARKERS_MAX >= 6
    {MT_ADV_CALLBACK, 5, "MARKER %d", menu_marker_sel_acb},
#endif
#if MARKERS_MAX >= 7
    {MT_ADV_CALLBACK, 6, "MARKER %d", menu_marker_sel_acb},
#endif
#if MARKERS_MAX >= 8
    {MT_ADV_CALLBACK, 7, "MARKER %d", menu_marker_sel_acb},
#endif
    {MT_CALLBACK, 0, "ALL OFF", menu_marker_disable_all_cb},
    {MT_ADV_CALLBACK, 0, "DELTA", menu_marker_delta_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_marker_s21smith[] = {
    {MT_ADV_CALLBACK, MS_LIN, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_LOG, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_REIM, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_SHUNT_RX, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_SHUNT_RLC, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_SERIES_RX, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_SERIES_RLC, "%s", menu_marker_smith_acb},
    {MT_NEXT, 0, NULL, (const void*)menu_back} // next-> menu_back
};

const menuitem_t menu_marker_s11smith[] = {
    {MT_ADV_CALLBACK, MS_LIN, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_LOG, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_REIM, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_RX, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_RLC, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_GB, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_GLC, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_RpXp, "%s", menu_marker_smith_acb},
    {MT_ADV_CALLBACK, MS_RpLC, "%s", menu_marker_smith_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

#ifdef __VNA_MEASURE_MODULE__
// Select menu depend from measure mode
#ifdef __USE_LC_MATCHING__
const menuitem_t menu_measure_lc[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_LC_MATH, "L/C MATCH", menu_measure_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

#ifdef __S11_CABLE_MEASURE__
const menuitem_t menu_measure_cable[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_S11_CABLE, "CABLE\n (S11)", menu_measure_acb},
    {MT_ADV_CALLBACK, KM_VELOCITY_FACTOR, "VELOCITY F.\n " R_LINK_COLOR "%d%%%%",
     menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_ACTUAL_CABLE_LEN, "CABLE LENGTH", menu_keyboard_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

#ifdef __S11_RESONANCE_MEASURE__
const menuitem_t menu_measure_resonance[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_S11_RESONANCE, "RESONANCE\n (S11)", menu_measure_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

#ifdef __S21_MEASURE__
const menuitem_t menu_measure_s21[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SHUNT_LC, "SHUNT LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_LC, "SERIES LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_XTAL, "SERIES\nXTAL (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, KM_MEASURE_R, " Rl = " R_LINK_COLOR "%b.4F" S_OHM, menu_keyboard_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_measure_filter[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_FILTER, "FILTER\n (S21)", menu_measure_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

const menuitem_t menu_measure[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
#ifdef __USE_LC_MATCHING__
    {MT_ADV_CALLBACK, MEASURE_LC_MATH, "L/C MATCH", menu_measure_acb},
#endif
#ifdef __S11_CABLE_MEASURE__
    {MT_ADV_CALLBACK, MEASURE_S11_CABLE, "CABLE\n (S11)", menu_measure_acb},
#endif
#ifdef __S11_RESONANCE_MEASURE__
    {MT_ADV_CALLBACK, MEASURE_S11_RESONANCE, "RESONANCE\n (S11)", menu_measure_acb},
#endif
#ifdef __S21_MEASURE__
    {MT_ADV_CALLBACK, MEASURE_SHUNT_LC, "SHUNT LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_LC, "SERIES LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_XTAL, "SERIES\nXTAL (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_FILTER, "FILTER\n (S21)", menu_measure_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

// Dynamic menu selector depend from measure mode
const menuitem_t* menu_measure_list[] = {
    [MEASURE_NONE] = menu_measure,
#ifdef __USE_LC_MATCHING__
    [MEASURE_LC_MATH] = menu_measure_lc,
#endif
#ifdef __S21_MEASURE__
    [MEASURE_SHUNT_LC] = menu_measure_s21,
    [MEASURE_SERIES_LC] = menu_measure_s21,
    [MEASURE_SERIES_XTAL] = menu_measure_s21,
    [MEASURE_FILTER] = menu_measure_filter,
#endif
#ifdef __S11_CABLE_MEASURE__
    [MEASURE_S11_CABLE] = menu_measure_cable,
#endif
#ifdef __S11_RESONANCE_MEASURE__
    [MEASURE_S11_RESONANCE] = menu_measure_resonance,
#endif
};
#endif

const menuitem_t menu_marker[] = {
    {MT_SUBMENU, 0, "SELECT\nMARKER", menu_marker_sel},
    {MT_ADV_CALLBACK, 0, "TRACKING", menu_marker_tracking_acb},
    {MT_ADV_CALLBACK, VNA_MODE_SEARCH, "SEARCH\n " R_LINK_COLOR "%s", menu_vna_mode_acb},
    {MT_CALLBACK, MK_SEARCH_LEFT, "SEARCH\n " S_LARROW "LEFT", menu_marker_search_dir_cb},
    {MT_CALLBACK, MK_SEARCH_RIGHT, "SEARCH\n " S_RARROW "RIGHT", menu_marker_search_dir_cb},
    {MT_CALLBACK, ST_START, "MOVE\nSTART", menu_marker_op_cb},
    {MT_CALLBACK, ST_STOP, "MOVE\nSTOP", menu_marker_op_cb},
    {MT_CALLBACK, ST_CENTER, "MOVE\nCENTER", menu_marker_op_cb},
    {MT_CALLBACK, ST_SPAN, "MOVE\nSPAN", menu_marker_op_cb},
    {MT_CALLBACK, UI_MARKER_EDELAY, "MARKER\nE-DELAY", menu_marker_op_cb},
    {MT_ADV_CALLBACK, 0, "DELTA", menu_marker_delta_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

#ifdef __DFU_SOFTWARE_MODE__
const menuitem_t menu_dfu[] = {
    {MT_CALLBACK, 0, "RESET AND\nENTER DFU", menu_dfu_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

#ifdef __USE_SERIAL_CONSOLE__
const menuitem_t menu_serial_speed[] = {
    {MT_ADV_CALLBACK, 0, "%u", menu_serial_speed_acb},
    {MT_ADV_CALLBACK, 1, "%u", menu_serial_speed_acb},
    {MT_ADV_CALLBACK, 2, "%u", menu_serial_speed_acb},
    {MT_ADV_CALLBACK, 3, "%u", menu_serial_speed_acb},
    {MT_ADV_CALLBACK, 4, "%u", menu_serial_speed_acb},
    {MT_ADV_CALLBACK, 5, "%u", menu_serial_speed_acb},
    {MT_ADV_CALLBACK, 6, "%u", menu_serial_speed_acb},
    {MT_ADV_CALLBACK, 7, "%u", menu_serial_speed_acb},
    {MT_ADV_CALLBACK, 8, "%u", menu_serial_speed_acb},
    {MT_ADV_CALLBACK, 9, "%u", menu_serial_speed_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_connection[] = {
    {MT_ADV_CALLBACK, VNA_MODE_CONNECTION, "CONNECTION\n " R_LINK_COLOR "%s", menu_vna_mode_acb},
    {MT_ADV_CALLBACK, 0, "SERIAL SPEED\n " R_LINK_COLOR "%u", menu_serial_speed_sel_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

const menuitem_t menu_clear[] = {
    {MT_CALLBACK, MENU_CONFIG_RESET, "CLEAR ALL\nAND RESET", menu_config_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

#ifdef USE_VARIABLE_OFFSET_MENU
const menuitem_t menu_offset[] = {
    {MT_ADV_CALLBACK, 0, "%d" S_Hz, menu_offset_acb},
    {MT_ADV_CALLBACK, 1, "%d" S_Hz, menu_offset_acb},
    {MT_ADV_CALLBACK, 2, "%d" S_Hz, menu_offset_acb},
    {MT_ADV_CALLBACK, 3, "%d" S_Hz, menu_offset_acb},
    {MT_ADV_CALLBACK, 4, "%d" S_Hz, menu_offset_acb},
    {MT_ADV_CALLBACK, 5, "%d" S_Hz, menu_offset_acb},
    {MT_ADV_CALLBACK, 6, "%d" S_Hz, menu_offset_acb},
    {MT_ADV_CALLBACK, 7, "%d" S_Hz, menu_offset_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

const menuitem_t menu_device1[] = {
    {MT_ADV_CALLBACK, 0, "MODE\n " R_LINK_COLOR "%s", menu_band_sel_acb},
#ifdef __DIGIT_SEPARATOR__
    {MT_ADV_CALLBACK, VNA_MODE_SEPARATOR, "SEPARATOR\n " R_LINK_COLOR "%s", menu_vna_mode_acb},
#endif
#ifdef __USB_UID__
    {MT_ADV_CALLBACK, VNA_MODE_USB_UID, "USB DEVICE\nUID", menu_vna_mode_acb},
#endif
    {MT_SUBMENU, 0, "CLEAR CONFIG", menu_clear},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

#ifdef __USE_RTC__
static UI_FUNCTION_ADV_CALLBACK(menu_rtc_out_acb) {
  (void)data;
  if (b) {
    if (rtc_clock_output_enabled()) {
      b->icon = BUTTON_ICON_CHECK;
      b->p1.text = "ON";
    } else
      b->p1.text = "OFF";
    return;
  }
  rtc_clock_output_toggle();
}

const menuitem_t menu_rtc[] = {
    {MT_ADV_CALLBACK, KM_RTC_DATE, "SET DATE", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_RTC_TIME, "SET TIME", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_RTC_CAL, "RTC CAL\n " R_LINK_COLOR "%+b.3f" S_PPM, menu_keyboard_acb},
    {MT_ADV_CALLBACK, 0, "RTC 512" S_Hz "\n Led2 %s", menu_rtc_out_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

const menuitem_t menu_device[] = {
    {MT_ADV_CALLBACK, KM_THRESHOLD, "THRESHOLD\n " R_LINK_COLOR "%.6q", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_XTAL, "TCXO\n " R_LINK_COLOR "%.6q", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_VBAT, "VBAT OFFSET\n " R_LINK_COLOR "%um" S_VOLT, menu_keyboard_acb},
#ifdef USE_VARIABLE_OFFSET_MENU
    {MT_ADV_CALLBACK, 0, "IF OFFSET\n " R_LINK_COLOR "%d" S_Hz, menu_offset_sel_acb},
#endif
#ifdef __USE_BACKUP__
    {MT_ADV_CALLBACK, VNA_MODE_BACKUP, "REMEMBER\nSTATE", menu_vna_mode_acb},
#endif
#ifdef __FLIP_DISPLAY__
    {MT_ADV_CALLBACK, VNA_MODE_FLIP_DISPLAY, "FLIP\nDISPLAY", menu_vna_mode_acb},
#endif
#ifdef __DFU_SOFTWARE_MODE__
    {MT_SUBMENU, 0, S_RARROW "DFU", menu_dfu},
#endif
    {MT_SUBMENU, 0, S_RARROW " MORE", menu_device1},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_system[] = {
    {MT_CALLBACK, MENU_CONFIG_TOUCH_CAL, "TOUCH CAL", menu_config_cb},
    {MT_CALLBACK, MENU_CONFIG_TOUCH_TEST, "TOUCH TEST", menu_config_cb},
#ifdef __LCD_BRIGHTNESS__
    {MT_ADV_CALLBACK, 0, "BRIGHTNESS\n " R_LINK_COLOR "%d%%%%", menu_brightness_acb},
#endif
    {MT_CALLBACK, MENU_CONFIG_SAVE, "SAVE CONFIG", menu_config_cb},
#if defined(__SD_CARD_LOAD__) && !SD_BROWSER_ENABLED
    {MT_CALLBACK, MENU_CONFIG_LOAD, "LOAD CONFIG", menu_config_cb},
#endif
    {MT_CALLBACK, MENU_CONFIG_VERSION, "VERSION", menu_config_cb},
#ifdef __USE_RTC__
    {MT_SUBMENU, 0, "DATE/TIME", menu_rtc},
#endif
    {MT_SUBMENU, 0, "DEVICE", menu_device},
#ifdef __USE_SERIAL_CONSOLE__
    {MT_SUBMENU, 0, "CONNECTION", menu_connection},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

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
const menuitem_t* menu_stack[MENU_STACK_DEPTH_MAX] = {menu_top, NULL, NULL, NULL, NULL};

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

static int get_lines_count(const char* label) {
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

static void menu_move_back(bool leave_ui) {
  if (menu_current_level == 0)
    return;
  menu_current_level--;
  ensure_selection();
  if (leave_ui)
    ui_mode_normal();
}

static void menu_set_submenu(const menuitem_t* submenu) {
  menu_stack[menu_current_level] = submenu;
  ensure_selection();
}

static void menu_push_submenu(const menuitem_t* submenu) {
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
  if ((status & (EVT_DOWN | EVT_UP)) == 0) {
    return;
  }
  uint32_t mask = 1U << selection;
  if (status & EVT_UP)
    selection++;
  if (status & EVT_DOWN)
    selection--;
  if ((uint16_t)selection >= count) {
    ui_mode_normal();
    return;
  }
  menu_draw(mask | (1U << selection));
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
enum { NUM_KEYBOARD, TXT_KEYBOARD };

// Keyboard size and position data
static const keypad_pos_t key_pos[] = {
    [NUM_KEYBOARD] = {KP_X_OFFSET, KP_Y_OFFSET, KP_WIDTH, KP_HEIGHT},
    [TXT_KEYBOARD] = {KPF_X_OFFSET, KPF_Y_OFFSET, KPF_WIDTH, KPF_HEIGHT}};

static const keypads_t keypads_freq[] = {
    {16, NUM_KEYBOARD},               // 16 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9 G
    {0x02, KP_1},                     // 4 5 6 M
    {0x12, KP_2},                     // 1 2 3 k
    {0x22, KP_3},                     // 0 . < x
    {0x01, KP_4},       {0x11, KP_5}, {0x21, KP_6}, {0x00, KP_7},  {0x10, KP_8}, {0x20, KP_9},
    {0x30, KP_G},       {0x31, KP_M}, {0x32, KP_k}, {0x33, KP_X1}, {0x23, KP_BS}};

static const keypads_t keypads_ufloat[] = {
    //
    {16, NUM_KEYBOARD},               // 13 + 3 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6
    {0x12, KP_2},                     // 1 2 3
    {0x22, KP_3},                     // 0 . < x
    {0x01, KP_4},       {0x11, KP_5},     {0x21, KP_6},     {0x00, KP_7},
    {0x10, KP_8},       {0x20, KP_9},     {0x33, KP_ENTER}, {0x23, KP_BS},
    {0x30, KP_EMPTY},   {0x31, KP_EMPTY}, {0x32, KP_EMPTY},
};

static const keypads_t keypads_percent[] = {
    //
    {16, NUM_KEYBOARD},               // 13 + 3 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6
    {0x12, KP_2},                     // 1 2 3
    {0x22, KP_3},                     // 0 . < %
    {0x01, KP_4},       {0x11, KP_5},     {0x21, KP_6},       {0x00, KP_7},
    {0x10, KP_8},       {0x20, KP_9},     {0x33, KP_PERCENT}, {0x23, KP_BS},
    {0x30, KP_EMPTY},   {0x31, KP_EMPTY}, {0x32, KP_EMPTY},
};

static const keypads_t keypads_float[] = {
    {16, NUM_KEYBOARD},               // 14 + 2 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6
    {0x12, KP_2},                     // 1 2 3 -
    {0x22, KP_3},                     // 0 . < x
    {0x01, KP_4},       {0x11, KP_5},     {0x21, KP_6},     {0x00, KP_7},
    {0x10, KP_8},       {0x20, KP_9},     {0x32, KP_MINUS}, {0x33, KP_ENTER},
    {0x23, KP_BS},      {0x30, KP_EMPTY}, {0x31, KP_EMPTY},
};

static const keypads_t keypads_mfloat[] = {{16, NUM_KEYBOARD}, // 16 buttons NUM keyboard (4x4 size)
                                           {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9 u
                                           {0x02, KP_1},                     // 4 5 6 m
                                           {0x12, KP_2},                     // 1 2 3 -
                                           {0x22, KP_3},                     // 0 . < x
                                           {0x01, KP_4},       {0x11, KP_5}, {0x21, KP_6},
                                           {0x00, KP_7},       {0x10, KP_8}, {0x20, KP_9},
                                           {0x30, KP_u},       {0x31, KP_m}, {0x32, KP_MINUS},
                                           {0x33, KP_ENTER},   {0x23, KP_BS}};

static const keypads_t keypads_mkufloat[] = {
    {16, NUM_KEYBOARD},               // 15 + 1 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6 m
    {0x12, KP_2},                     // 1 2 3 k
    {0x22, KP_3},                     // 0 . < x
    {0x01, KP_4},       {0x11, KP_5}, {0x21, KP_6},  {0x00, KP_7},  {0x10, KP_8},     {0x20, KP_9},
    {0x31, KP_m},       {0x32, KP_k}, {0x33, KP_X1}, {0x23, KP_BS}, {0x30, KP_EMPTY},
};

static const keypads_t keypads_nfloat[] = {
    {16, NUM_KEYBOARD},               // 16 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9 u
    {0x02, KP_1},                     // 4 5 6 n
    {0x12, KP_2},                     // 1 2 3 p
    {0x22, KP_3},                     // 0 . < -
    {0x01, KP_4},       {0x11, KP_5}, {0x21, KP_6}, {0x00, KP_7},     {0x10, KP_8}, {0x20, KP_9},
    {0x30, KP_u},       {0x31, KP_n}, {0x32, KP_p}, {0x33, KP_MINUS}, {0x23, KP_BS}};

#if 0
//  ABCD keyboard
static const keypads_t keypads_text[] = {
  {40, TXT_KEYBOARD },   // 40 buttons TXT keyboard (10x4 size)
  {0x00, '0'}, {0x10, '1'}, {0x20, '2'}, {0x30, '3'}, {0x40, '4'}, {0x50, '5'}, {0x60, '6'}, {0x70, '7'}, {0x80, '8'}, {0x90, '9'},
  {0x01, 'A'}, {0x11, 'B'}, {0x21, 'C'}, {0x31, 'D'}, {0x41, 'E'}, {0x51, 'F'}, {0x61, 'G'}, {0x71, 'H'}, {0x81, 'I'}, {0x91, 'J'},
  {0x02, 'K'}, {0x12, 'L'}, {0x22, 'M'}, {0x32, 'N'}, {0x42, 'O'}, {0x52, 'P'}, {0x62, 'Q'}, {0x72, 'R'}, {0x82, 'S'}, {0x92, 'T'},
  {0x03, 'U'}, {0x13, 'V'}, {0x23, 'W'}, {0x33, 'X'}, {0x43, 'Y'}, {0x53, 'Z'}, {0x63, '_'}, {0x73, '-'}, {0x83, S_LARROW[0]}, {0x93, S_ENTER[0]},
};
#else
// QWERTY keyboard
static const keypads_t keypads_text[] = {
    {40, TXT_KEYBOARD}, // 40 buttons TXT keyboard (10x4 size)
    {0x00, '1'},        {0x10, '2'}, {0x20, '3'}, {0x30, '4'},         {0x40, '5'},
    {0x50, '6'},        {0x60, '7'}, {0x70, '8'}, {0x80, '9'},         {0x90, '0'},
    {0x01, 'Q'},        {0x11, 'W'}, {0x21, 'E'}, {0x31, 'R'},         {0x41, 'T'},
    {0x51, 'Y'},        {0x61, 'U'}, {0x71, 'I'}, {0x81, 'O'},         {0x91, 'P'},
    {0x02, 'A'},        {0x12, 'S'}, {0x22, 'D'}, {0x32, 'F'},         {0x42, 'G'},
    {0x52, 'H'},        {0x62, 'J'}, {0x72, 'K'}, {0x82, 'L'},         {0x92, '_'},
    {0x03, '-'},        {0x13, 'Z'}, {0x23, 'X'}, {0x33, 'C'},         {0x43, 'V'},
    {0x53, 'B'},        {0x63, 'N'}, {0x73, 'M'}, {0x83, S_LARROW[0]}, {0x93, S_ENTER[0]},
};
#endif

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
static const keypads_t* keypad_type_list[] = {
    [KEYPAD_FREQ] = keypads_freq,         // frequency input
    [KEYPAD_UFLOAT] = keypads_ufloat,     // unsigned float input
    [KEYPAD_PERCENT] = keypads_percent,   // unsigned float input in percent
    [KEYPAD_FLOAT] = keypads_float,       //   signed float input
    [KEYPAD_MFLOAT] = keypads_mfloat,     //   signed milli/micro float input
    [KEYPAD_MKUFLOAT] = keypads_mkufloat, // unsigned milli/kilo float input
    [KEYPAD_NFLOAT] = keypads_nfloat,     //   signed micro/nano/pico float input
    [KEYPAD_TEXT] = keypads_text          // text input
};

// Get value from keyboard functions
float keyboard_get_float(void) {
  return my_atof(kp_buf);
}
freq_t keyboard_get_freq(void) {
  return my_atoui(kp_buf);
}
uint32_t keyboard_get_uint(void) {
  return my_atoui(kp_buf);
}
int32_t keyboard_get_int(void) {
  return my_atoi(kp_buf);
}

// Keyboard call back functions, allow get value for Keyboard menu button (see menu_keyboard_acb)
// and apply on finish input
UI_KEYBOARD_CALLBACK(input_freq) {
  if (b) {
    if (data == ST_VAR && var_freq)
      plot_printf(b->label, sizeof(b->label), "JOG STEP\n " R_LINK_COLOR "%.3q" S_Hz, var_freq);
    if (data == ST_STEP)
      b->p1.f = (float)get_sweep_frequency(ST_SPAN) / (sweep_points - 1);
    return;
  }
  set_sweep_frequency(data, keyboard_get_freq());
}

UI_KEYBOARD_CALLBACK(input_var_delay) {
  (void)data;
  if (b) {
    if (current_props._var_delay)
      plot_printf(b->label, sizeof(b->label), "JOG STEP\n " R_LINK_COLOR "%F" S_SECOND,
                  current_props._var_delay);
    return;
  }
  current_props._var_delay = keyboard_get_float();
}

// Call back functions for MT_CALLBACK type
UI_KEYBOARD_CALLBACK(input_points) {
  (void)data;
  if (b) {
    b->p1.u = sweep_points;
    return;
  }
  set_sweep_points(keyboard_get_uint());
}

UI_KEYBOARD_CALLBACK(input_amplitude) {
  int type = trace[current_trace].type;
  float scale = get_trace_scale(current_trace);
  float ref = get_trace_refpos(current_trace);
  float bot = (0 - ref) * scale;
  float top = (NGRIDY - ref) * scale;

  if (b) {
    float val = data == 0 ? top : bot;
    if (type == TRC_SWR)
      val += 1.0f;
    plot_printf(b->label, sizeof(b->label), "%s\n " R_LINK_COLOR "%.4F%s",
                data == 0 ? "TOP" : "BOTTOM", val, trace_info_list[type].symbol);
    return;
  }
  float value = keyboard_get_float();
  if (type == TRC_SWR)
    value -= 1.0f; // Hack for SWR trace!
  if (data == 0)
    top = value; // top value input
  else
    bot = value; // bottom value input
  scale = (top - bot) / NGRIDY;
  ref = (top == bot) ? -value : -bot / scale;
  set_trace_scale(current_trace, scale);
  set_trace_refpos(current_trace, ref);
}

UI_KEYBOARD_CALLBACK(input_scale) {
  (void)data;
  if (b)
    return;
  set_trace_scale(current_trace, keyboard_get_float());
}

UI_KEYBOARD_CALLBACK(input_ref) {
  (void)data;
  if (b)
    return;
  set_trace_refpos(current_trace, keyboard_get_float());
}

UI_KEYBOARD_CALLBACK(input_edelay) {
  (void)data;
  if (current_trace == TRACE_INVALID)
    return;
  int ch = trace[current_trace].channel;
  if (b) {
    plot_printf(b->label, sizeof(b->label), "E-DELAY S%d1\n " R_LINK_COLOR "%.7F" S_SECOND, ch + 1,
                current_props._electrical_delay[ch]);
    return;
  }
  set_electrical_delay(ch, keyboard_get_float());
}

UI_KEYBOARD_CALLBACK(input_s21_offset) {
  (void)data;
  if (b) {
    b->p1.f = s21_offset;
    return;
  }
  set_s21_offset(keyboard_get_float());
}

UI_KEYBOARD_CALLBACK(input_velocity) {
  (void)data;
  if (b) {
    b->p1.u = velocity_factor;
    return;
  }
  velocity_factor = keyboard_get_uint();
}

#ifdef __S11_CABLE_MEASURE__
extern float real_cable_len;
UI_KEYBOARD_CALLBACK(input_cable_len) {
  (void)data;
  if (b) {
    if (real_cable_len == 0.0f)
      return;
    plot_printf(b->label, sizeof(b->label), "%s\n " R_LINK_COLOR "%.4F%s", "CABLE LENGTH",
                real_cable_len, S_METRE);
    return;
  }
  real_cable_len = keyboard_get_float();
}
#endif

UI_KEYBOARD_CALLBACK(input_xtal) {
  (void)data;
  if (b) {
    b->p1.u = config._xtal_freq;
    return;
  }
  si5351_set_tcxo(keyboard_get_uint());
}

UI_KEYBOARD_CALLBACK(input_harmonic) {
  (void)data;
  if (b) {
    b->p1.u = config._harmonic_freq_threshold;
    return;
  }
  uint32_t value = keyboard_get_uint();
  config._harmonic_freq_threshold = clamp_harmonic_threshold(value);
  config_service_notify_configuration_changed();
}

UI_KEYBOARD_CALLBACK(input_vbat) {
  (void)data;
  if (b) {
    b->p1.u = config._vbat_offset;
    return;
  }
  config._vbat_offset = keyboard_get_uint();
  config_service_notify_configuration_changed();
}

#ifdef __S21_MEASURE__
UI_KEYBOARD_CALLBACK(input_measure_r) {
  (void)data;
  if (b) {
    b->p1.f = config._measure_r;
    return;
  }
  config._measure_r = keyboard_get_float();
  config_service_notify_configuration_changed();
}
#endif

#ifdef __VNA_Z_RENORMALIZATION__
UI_KEYBOARD_CALLBACK(input_portz) {
  if (b) {
    b->p1.f = data ? current_props._cal_load_r : current_props._portz;
    return;
  }
  if (data)
    current_props._cal_load_r = keyboard_get_float();
  else
    current_props._portz = keyboard_get_float();
}
#endif

#ifdef __USE_RTC__
UI_KEYBOARD_CALLBACK(input_date_time) {
  if (b)
    return;
  int i = 0;
  uint32_t dt_buf[2];
  dt_buf[0] = rtc_get_tr_bcd(); // TR should be read first for sync
  dt_buf[1] = rtc_get_dr_bcd(); // DR should be read second
  //            0    1   2       4      5     6
  // time[] ={sec, min, hr, 0, day, month, year, 0}
  uint8_t* time = (uint8_t*)dt_buf;
  for (; i < 6 && kp_buf[i] != 0; i++)
    kp_buf[i] -= '0';
  for (; i < 6; i++)
    kp_buf[i] = 0;
  for (i = 0; i < 3; i++)
    kp_buf[i] = (kp_buf[2 * i] << 4) | kp_buf[2 * i + 1]; // BCD format
  if (data == KM_RTC_DATE) {
    // Month limit 1 - 12 (in BCD)
    if (kp_buf[1] < 1)
      kp_buf[1] = 1;
    else if (kp_buf[1] > 0x12)
      kp_buf[1] = 0x12;
    // Day limit (depend from month):
    uint8_t day_max = 28 + ((0b11101100000000000010111110111011001100 >> (kp_buf[1] << 1)) & 3);
    day_max = ((day_max / 10) << 4) | (day_max % 10); // to BCD
    if (kp_buf[2] < 1)
      kp_buf[2] = 1;
    else if (kp_buf[2] > day_max)
      kp_buf[2] = day_max;
    time[6] = kp_buf[0]; // year
    time[5] = kp_buf[1]; // month
    time[4] = kp_buf[2]; // day
  } else {
    // Hour limit 0 - 23, min limit 0 - 59, sec limit 0 - 59 (in BCD)
    if (kp_buf[0] > 0x23)
      kp_buf[0] = 0x23;
    if (kp_buf[1] > 0x59)
      kp_buf[1] = 0x59;
    if (kp_buf[2] > 0x59)
      kp_buf[2] = 0x59;
    time[2] = kp_buf[0]; // hour
    time[1] = kp_buf[1]; // min
    time[0] = kp_buf[2]; // sec
  }
  rtc_set_time(dt_buf[1], dt_buf[0]);
}

UI_KEYBOARD_CALLBACK(input_rtc_cal) {
  (void)data;
  if (b) {
    b->p1.f = rtc_get_cal();
    return;
  }
  rtc_set_cal(keyboard_get_float());
}
#endif

#ifdef __USE_SD_CARD__
UI_KEYBOARD_CALLBACK(input_filename) {
  if (b)
    return;
  ui_save_file(kp_buf, data);
}
#endif

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

static void keypad_draw_button(int id) {
  if (id < 0)
    return;
  button_t button;
  button.fg = LCD_MENU_TEXT_COLOR;

  if (id == selection) {
    button.bg = LCD_MENU_ACTIVE_COLOR;
    button.border = KEYBOARD_BUTTON_BORDER | BUTTON_BORDER_FALLING;
  } else {
    button.bg = LCD_MENU_COLOR;
    button.border = KEYBOARD_BUTTON_BORDER | BUTTON_BORDER_RISE;
  }

  const keypad_pos_t* p = &key_pos[keypads->type];
  int x = p->x_offs + (keypads->buttons[id].pos >> 4) * p->width;
  int y = p->y_offs + (keypads->buttons[id].pos & 0xF) * p->height;
  ui_draw_button(x, y, p->width, p->height, &button);
  uint8_t ch = keypads->buttons[id].c;
  if (ch == KP_EMPTY)
    return;
  if (keypads->type == NUM_KEYBOARD) {
    lcd_drawfont(ch, x + (KP_WIDTH - NUM_FONT_GET_WIDTH) / 2,
                 y + (KP_HEIGHT - NUM_FONT_GET_HEIGHT) / 2);
  } else {
#if 0
    lcd_drawchar(ch,
                     x + (KPF_WIDTH - FONT_WIDTH) / 2,
                     y + (KPF_HEIGHT - FONT_GET_HEIGHT) / 2);
#else
    lcd_drawchar_size(ch, x + KPF_WIDTH / 2 - FONT_WIDTH + 1, y + KPF_HEIGHT / 2 - FONT_GET_HEIGHT,
                      2);
#endif
  }
}

static void draw_keypad(void) {
  for (int i = 0; i < keypads->size; i++)
    keypad_draw_button(i);
}

static int period_pos(void) {
  int j;
  for (j = 0; kp_buf[j] && kp_buf[j] != '.'; j++)
    ;
  return j;
}

static void draw_numeric_area_frame(void) {
  lcd_set_colors(LCD_INPUT_TEXT_COLOR, LCD_INPUT_BG_COLOR);
  lcd_fill(0, LCD_HEIGHT - NUM_INPUT_HEIGHT, LCD_WIDTH, NUM_INPUT_HEIGHT);
  const char* label = keypads_mode_tbl[keypad_mode].name;
  int lines = get_lines_count(label);
  lcd_drawstring(10, LCD_HEIGHT - (FONT_STR_HEIGHT * lines + NUM_INPUT_HEIGHT) / 2, label);
}

static void draw_numeric_input(const char* buf) {
  uint16_t x = 14 + FONT_STR_WIDTH(12), space;
  uint16_t y = LCD_HEIGHT - (NUM_FONT_GET_HEIGHT + NUM_INPUT_HEIGHT) / 2;
  uint16_t xsim;
#ifdef __USE_RTC__
  if ((1 << keypad_mode) & ((1 << KM_RTC_DATE) | (1 << KM_RTC_TIME)))
    xsim = 0b01010100;
  else
#endif
    xsim = (0b00100100100100100 >> (2 - (period_pos() % 3))) & (~1);
  lcd_set_colors(LCD_INPUT_TEXT_COLOR, LCD_INPUT_BG_COLOR);
  while (*buf) {
    int c = *buf++;
    if (c == '.') {
      c = KP_PERIOD;
      xsim <<= 4;
    } else if (c == '-') {
      c = KP_MINUS;
      xsim &= ~3;
    } else if (c >= '0' && c <= '9')
      c -= '0';
    else
      continue;
    // Add space before char
    space = 2 + 10 * (xsim & 1);
    xsim >>= 1;
    lcd_fill(x, y, space, NUM_FONT_GET_HEIGHT);
    x += space;
    lcd_drawfont(c, x, y);
    x += NUM_FONT_GET_WIDTH;
  }
  lcd_fill(x, y, NUM_FONT_GET_WIDTH + 2 + 10, NUM_FONT_GET_HEIGHT);
}

static void draw_text_input(const char* buf) {
  lcd_set_colors(LCD_INPUT_TEXT_COLOR, LCD_INPUT_BG_COLOR);
#if 0
  uint16_t x = 14 + FONT_STR_WIDTH(5);
  uint16_t y = LCD_HEIGHT-(FONT_GET_HEIGHT + NUM_INPUT_HEIGHT)/2;
  lcd_fill(x, y, FONT_STR_WIDTH(20), FONT_GET_HEIGHT);
  lcd_printf(x, y, buf);
#else
  int n = 2;
  uint16_t x = 14 + FONT_STR_WIDTH(5);
  uint16_t y = LCD_HEIGHT - (FONT_GET_HEIGHT * n + NUM_INPUT_HEIGHT) / 2;
  lcd_fill(x, y, FONT_STR_WIDTH(20) * n, FONT_GET_HEIGHT * n);
  lcd_drawstring_size(buf, x, y, n);
#endif
}

//=====================================================================================================
//                                     Keyboard UI processing
//=====================================================================================================
enum { K_CONTINUE = 0, K_DONE, K_CANCEL };
static int num_keypad_click(int c, int kp_index) {
  if (c >= KP_k && c <= KP_PERCENT) {
    if (kp_index == 0)
      return K_CANCEL;
    if (c >= KP_k && c <= KP_G) { // Apply k, M, G input (add zeroes and shift . right)
      uint16_t scale = c - KP_k + 1;
      scale += (scale << 1);
      int i = period_pos();
      if (scale + i > NUMINPUT_LEN)
        scale = NUMINPUT_LEN - i;
      do {
        char v = kp_buf[i + 1];
        if (v == 0 || kp_buf[i] == 0) {
          v = '0';
          kp_buf[i + 2] = 0;
        }
        kp_buf[i + 1] = kp_buf[i];
        kp_buf[i++] = v;
      } while (--scale);
    } else if (c >= KP_m &&
               c <= KP_p) { // Apply m, u, n, p input (add format at end for atof function)
      const char prefix[] = {'m', 'u', 'n', 'p'};
      kp_buf[kp_index] = prefix[c - KP_m];
      kp_buf[kp_index + 1] = 0;
    }
    return K_DONE;
  }
#ifdef __USE_RTC__
  int maxlength = (1 << keypad_mode) & ((1 << KM_RTC_DATE) | (1 << KM_RTC_TIME)) ? 6 : NUMINPUT_LEN;
#else
  int maxlength = NUMINPUT_LEN;
#endif
  if (c == KP_BS) {
    if (kp_index == 0)
      return K_CANCEL;
    --kp_index;
  } else if (c == KP_MINUS) {
    int i;
    if (kp_buf[0] == '-') {
      for (i = 0; i < NUMINPUT_LEN; i++)
        kp_buf[i] = kp_buf[i + 1];
      --kp_index;
    } else {
      for (i = NUMINPUT_LEN; i > 0; i--)
        kp_buf[i] = kp_buf[i - 1];
      kp_buf[0] = '-';
      if (kp_index < maxlength)
        ++kp_index;
    }
  } else if (kp_index < maxlength) {
    if (c <= KP_9)
      kp_buf[kp_index++] = '0' + c;
    else if (c == KP_PERIOD && kp_index == period_pos() &&
             maxlength == NUMINPUT_LEN) // append period if there are no period and for num input
                                        // (skip for date/time)
      kp_buf[kp_index++] = '.';
  }
  kp_buf[kp_index] = '\0';
  draw_numeric_input(kp_buf);
  return K_CONTINUE;
}

static int txt_keypad_click(int c, int kp_index) {
  if (c == S_ENTER[0]) { // Enter
    return kp_index == 0 ? K_CANCEL : K_DONE;
  }
  if (c == S_LARROW[0]) { // Backspace
    if (kp_index == 0)
      return K_CANCEL;
    --kp_index;
  } else if (kp_index < TXTINPUT_LEN) { // any other text input
    kp_buf[kp_index++] = c;
  }
  kp_buf[kp_index] = '\0';
  draw_text_input(kp_buf);
  return K_CONTINUE;
}

static void ui_mode_keypad(int mode) {
  if (ui_mode == UI_KEYPAD)
    return;
  ui_mode = UI_KEYPAD;
  set_area_size(0, 0);
  // keypads array
  keypad_mode = mode;
  keypads = keypad_type_list[keypads_mode_tbl[mode].keypad_type];
  selection = -1;
  kp_buf[0] = 0;
  //  menu_draw(-1);
  draw_keypad();
  draw_numeric_area_frame();
}

#if SD_BROWSER_ENABLED
void ui_mode_browser(int mode) {
  if (ui_mode == UI_BROWSER)
    return;
  ui_mode = UI_BROWSER;
  set_area_size(0, 0);
  selection = -1;
  sd_browser_enter(mode);
}
#endif

static void keypad_click(int key) {
  int c = keypads->buttons[key].c; // !!! Use key + 1 (zero key index used or size define)
  int index = strlen(kp_buf);
  int result =
      keypads->type == NUM_KEYBOARD ? num_keypad_click(c, index) : txt_keypad_click(c, index);
  if (result == K_DONE)
    ui_keyboard_cb(keypad_mode, NULL); // apply input done
  // Exit loop on done or cancel
  if (result != K_CONTINUE)
    ui_mode_normal();
}

static void ui_keypad_touch(int touch_x, int touch_y) {
  const keypad_pos_t* p = &key_pos[keypads->type];
  if (touch_x < p->x_offs || touch_y < p->y_offs)
    return;
  // Calculate key position from touch x and y
  touch_x -= p->x_offs;
  touch_x /= p->width;
  touch_y -= p->y_offs;
  touch_y /= p->height;
  uint8_t pos = (touch_y & 0x0F) | (touch_x << 4);
  for (int i = 0; i < keypads->size; i++) {
    if (keypads->buttons[i].pos != pos)
      continue;
    if (keypads->buttons[i].c == KP_EMPTY)
      break;
    int old = selection;
    keypad_draw_button(selection = i); // draw new focus
    keypad_draw_button(old);           // Erase old focus
    touch_wait_release();
    selection = -1;
    keypad_draw_button(i); // erase new focus
    keypad_click(i);       // Process input
    return;
  }
  return;
}

static void ui_keypad_lever(uint16_t status) {
  if (status == EVT_BUTTON_SINGLE_CLICK) {
    if (selection >= 0) // Process input
      keypad_click(selection);
    return;
  }
  if ((status & (EVT_DOWN | EVT_UP)) == 0)
    return;
  int keypads_last_index = keypads->size - 1;
  int old = selection;
  do {
    if ((status & EVT_DOWN) && --selection < 0)
      selection = keypads_last_index;
    if ((status & EVT_UP) && ++selection > keypads_last_index)
      selection = 0;
  } while (keypads->buttons[selection].c == KP_EMPTY);
  keypad_draw_button(old);
  keypad_draw_button(selection);
}
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
#if SD_BROWSER_ENABLED
  if (ui_mode == UI_KEYPAD || ui_mode == UI_BROWSER)
    request_to_redraw(REDRAW_ALL);
#else
  if (ui_mode == UI_KEYPAD)
    request_to_redraw(REDRAW_ALL);
#endif
  ui_mode = UI_NORMAL;
}

#define MARKER_SPEEDUP 3
static uint16_t marker_repeat_dir = 0;
static uint16_t marker_repeat_step = 1 << MARKER_SPEEDUP;

static void lever_move_marker(uint16_t status) {
  if (active_marker == MARKER_INVALID || !markers[active_marker].enabled)
    return;
  if ((status & (EVT_DOWN | EVT_UP)) == 0) {
    return;
  }
  uint16_t dir = status & (EVT_DOWN | EVT_UP);
  if ((status & EVT_REPEAT) == 0 || dir != marker_repeat_dir) {
    marker_repeat_step = 1 << MARKER_SPEEDUP;
    marker_repeat_dir = dir;
  } else if (marker_repeat_step < 0xFFFF) {
    marker_repeat_step++;
  }
  uint16_t step = marker_repeat_step >> MARKER_SPEEDUP;
  if (step == 0)
    step = 1;
  int idx = (int)markers[active_marker].index;
  if ((status & EVT_DOWN) && (idx -= step) < 0)
    idx = 0;
  if ((status & EVT_UP) && (idx += step) > sweep_points - 1)
    idx = sweep_points - 1;
  set_marker_index(active_marker, idx);
  redraw_marker(active_marker);
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
  if ((status & (EVT_DOWN | EVT_UP)) == 0)
    return;
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
  if (freq > FREQUENCY_MAX || freq < FREQUENCY_MIN)
    return;
  set_sweep_frequency(mode, freq);
}

#define STEPRATIO 0.2f
static void lever_edelay(uint16_t status) {
  if ((status & (EVT_DOWN | EVT_UP)) == 0)
    return;
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
#if SD_BROWSER_ENABLED
    [UI_BROWSER] = {ui_browser_lever, ui_browser_touch},
#endif
};

static void ui_process_lever(void) {
  const bool lever_event_requested = (operation_requested & OP_LEVER) != 0U;
  const systime_t now = chVTGetSystemTimeX();
  if (lever_event_requested) {
    uint16_t status = ui_input_check();
    operation_requested &= (uint8_t)~OP_LEVER;
    if (status != 0U) {
      const uint16_t buttons = ui_input_get_buttons();
      lever_repeat_state.mask = buttons_to_event_mask(buttons);
      if (lever_repeat_state.mask != 0U) {
        lever_repeat_state.next_tick = now + BUTTON_REPEAT_TICKS;
      } else {
        lever_repeat_state.next_tick = 0;
        marker_repeat_dir = 0;
        marker_repeat_step = 1 << MARKER_SPEEDUP;
      }
      ui_handler[ui_mode].button(status);
      return;
    }
  }
  if (lever_repeat_state.mask != 0U &&
      (int32_t)(now - lever_repeat_state.next_tick) >= 0) {
    const uint16_t buttons = ui_input_get_buttons();
    lever_repeat_state.mask = buttons_to_event_mask(buttons);
    if (lever_repeat_state.mask == 0U) {
      lever_repeat_state.next_tick = 0;
      marker_repeat_dir = 0;
      marker_repeat_step = 1 << MARKER_SPEEDUP;
      return;
    }
    lever_repeat_state.next_tick = now + BUTTON_REPEAT_TICKS;
    ui_handler[ui_mode].button(lever_repeat_state.mask | EVT_REPEAT);
  }
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
  // if (ui_mode >= UI_END) return; // for safe
  ui_process_lever();
  if (operation_requested & OP_TOUCH)
    ui_process_touch();

  touch_start_watchdog();
  operation_requested &= (uint8_t)~(OP_LEVER | OP_TOUCH);
}

void handle_button_interrupt(uint16_t channel) {
  (void)channel;
  operation_requested |= OP_LEVER;
  // cur_button = READ_PORT() & BUTTON_MASK;
}

// static systime_t t_time = 0;
//  Triggered touch interrupt call
void handle_touch_interrupt(void) {
  operation_requested |= OP_TOUCH;
  //  systime_t n_time = chVTGetSystemTimeX();
  //  shell_printf("%d\r\n", n_time - t_time);
  //  t_time = n_time;
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
