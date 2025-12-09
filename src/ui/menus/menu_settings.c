#include "nanovna.h"
#include "ui/ui_internal.h"
#include "ui/menus/menu_internal.h"
#include "ui/input/ui_touch.h"
#include "ui/input/ui_keypad.h"
#include "hal.h"
#include "chprintf.h"
#include <string.h>
#include "platform/peripherals/si5351.h"
#include "interfaces/cli/shell_service.h"
#include "ui/controller/ui_events.h"
#include "ui/input/hardware_input.h"
#include "infra/state/state_manager.h"
#include "infra/storage/config_service.h"

// Dependencies
// defined in runtime_entry.c or similar
extern const char* const info_about[];
extern const uint8_t touch_bitmap[];

#define TOUCH_MARK_W 11
#define TOUCH_MARK_H 11
#define TOUCH_MARK_X (TOUCH_MARK_W / 2)
#define TOUCH_MARK_Y (TOUCH_MARK_H / 2)
#define CALIBRATION_OFFSET 20

// ----------------------------------------------------------------------------
// Config Callback Enum
enum {
  MENU_CONFIG_TOUCH_CAL = 0,
  MENU_CONFIG_TOUCH_TEST,
  MENU_CONFIG_VERSION,
  MENU_CONFIG_SAVE,
  MENU_CONFIG_RESET,
#if defined(__SD_CARD_LOAD__) && !defined(__SD_FILE_BROWSER__)
  MENU_CONFIG_LOAD,
#endif
};

// ----------------------------------------------------------------------------
// Internal Functions

static void get_touch_point(uint16_t x, uint16_t y, const char* name, int16_t* data) {
  // Clear screen and ask for press
  lcd_set_colors(LCD_FG_COLOR, LCD_BG_COLOR);
  display_presenter_fill(0, 0, LCD_WIDTH, LCD_HEIGHT); // Replaced lcd_clear_screen
  lcd_blit_bitmap(x, y, TOUCH_MARK_W, TOUCH_MARK_H, (const uint8_t*)touch_bitmap);
  lcd_printf((LCD_WIDTH - FONT_STR_WIDTH(18)) / 2, (LCD_HEIGHT - FONT_GET_HEIGHT) / 2, "TOUCH %s *",
             name);
  // Wait release, and fill data
  touch_wait_release();
  touch_get_last_position(&data[0], &data[1]);
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

void ui_touch_draw_test(void) {
  int x0, y0;
  int x1, y1;
  lcd_set_colors(LCD_FG_COLOR, LCD_BG_COLOR);
  display_presenter_fill(0, 0, LCD_WIDTH, LCD_HEIGHT); // Replaced lcd_clear_screen
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

  display_presenter_fill(0, 0, LCD_WIDTH, LCD_HEIGHT); // Replaced lcd_clear_screen
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
  display_presenter_fill(0, 0, LCD_WIDTH, LCD_HEIGHT); // Replaced lcd_clear_screen
  lcd_drawstring(x, y,
                 "DFU: Device Firmware Update Mode\n"
                 "To exit DFU mode, please reset device yourself.");
  boardDFUEnter();
}
#endif

// ----------------------------------------------------------------------------
// Callbacks

UI_FUNCTION_CALLBACK(menu_config_cb) {
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
#if defined(__SD_CARD_LOAD__) && !defined(__SD_FILE_BROWSER__)
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
UI_FUNCTION_CALLBACK(menu_dfu_cb) {
  (void)data;
  ui_enter_dfu();
}

const menuitem_t menu_dfu[] = {
    {MT_CALLBACK, 0, "RESET AND\nENTER DFU", menu_dfu_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

// ----------------------------------------------------------------------------
// VNA MODE Handling

typedef struct {
  const char* text; // if 0, use checkbox
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

// Forward declaration if needed, or include proper header
// select_lever_mode is in ui_controller.c static?
extern bool select_lever_mode(int mode); // We might need to expose this or replicate logic

// We need apply_vna_mode exposed or static here
// It was global in ui_controller.c? No, void apply_vna_mode(...)
// It should be exposed or moved. If we move vna_mode_acb here, we need apply_vna_mode here.
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
//    select_lever_mode(LM_SEARCH); // Need to resolve select_lever_mode
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

UI_FUNCTION_ADV_CALLBACK(menu_vna_mode_acb) {
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

// ----------------------------------------------------------------------------
// Device / Serial / Bandwidth

#ifdef __USE_SERIAL_CONSOLE__
static const menu_descriptor_t menu_serial_speed_desc[] = {
    {MT_ADV_CALLBACK, 0}, {MT_ADV_CALLBACK, 1}, {MT_ADV_CALLBACK, 2},
    {MT_ADV_CALLBACK, 3}, {MT_ADV_CALLBACK, 4}, {MT_ADV_CALLBACK, 5},
    {MT_ADV_CALLBACK, 6}, {MT_ADV_CALLBACK, 7}, {MT_ADV_CALLBACK, 8},
    {MT_ADV_CALLBACK, 9},
};

UI_FUNCTION_ADV_CALLBACK(menu_serial_speed_acb) {
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

static const menuitem_t* menu_build_serial_speed_menu(void) {
  menuitem_t* cursor = menu_dynamic_acquire();
  cursor = ui_menu_list(menu_serial_speed_desc, ARRAY_COUNT(menu_serial_speed_desc), "%u",
                        menu_serial_speed_acb, cursor);
  menu_set_next(cursor, menu_back);
  return menu_dynamic_buffer;
}

UI_FUNCTION_ADV_CALLBACK(menu_serial_speed_sel_acb) {
  (void)data;
  if (b) {
    b->p1.u = config._serial_speed;
    return;
  }
  menu_push_submenu(menu_build_serial_speed_menu());
}

const menuitem_t menu_connection[] = {
    {MT_ADV_CALLBACK, VNA_MODE_CONNECTION, "CONNECTION\n " R_LINK_COLOR "%s", menu_vna_mode_acb},
    {MT_ADV_CALLBACK, 0, "SERIAL SPEED\n " R_LINK_COLOR "%u", menu_serial_speed_sel_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

// ----------------------------------------------------------------------------
// Offset

#ifdef USE_VARIABLE_OFFSET_MENU
UI_FUNCTION_ADV_CALLBACK(menu_offset_acb) {
  int32_t offset = (data + 1) * FREQUENCY_OFFSET_STEP;
  if (b) {
    b->icon = IF_OFFSET == offset ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = offset;
    return;
  }
  si5351_set_frequency_offset(offset);
}

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

UI_FUNCTION_ADV_CALLBACK(menu_offset_sel_acb) {
  (void)data;
  if (b) {
    b->p1.i = IF_OFFSET;
    return;
  }
  menu_push_submenu(menu_offset);
}
#endif

// ----------------------------------------------------------------------------
// Band Select / Device

static const option_desc_t band_mode_options[] = {
    {0, "Si5351", BUTTON_ICON_NONE},
    {1, "MS5351", BUTTON_ICON_NONE},
    {2, "SWC5351", BUTTON_ICON_NONE},
};

UI_FUNCTION_ADV_CALLBACK(menu_band_sel_acb) {
  (void)data;
  uint16_t mode = config._band_mode;
  ui_cycle_option(&mode, band_mode_options, ARRAY_COUNT(band_mode_options), b);
  if (b)
    return;
  if (config._band_mode == mode)
    return;
  config._band_mode = (uint8_t)mode;
  si5351_set_band_mode(config._band_mode);
  config_service_notify_configuration_changed();
}

const menuitem_t menu_clear[] = {
    {MT_CALLBACK, MENU_CONFIG_RESET, "CLEAR ALL\nAND RESET", menu_config_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

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

// ----------------------------------------------------------------------------
// Brightness

#ifdef __LCD_BRIGHTNESS__
// Brightness control range 0 - 100
void lcd_set_brightness(uint16_t b) {
  dac_setvalue_ch2(700 + b * (4000 - 700) / 100);
}

UI_FUNCTION_ADV_CALLBACK(menu_brightness_acb) {
  (void)data;
  if (b) {
    b->p1.u = config._brightness;
    return;
  }
  int16_t value = config._brightness;
  lcd_set_colors(LCD_MENU_TEXT_COLOR, LCD_MENU_COLOR);
  display_presenter_fill(LCD_WIDTH / 2 - FONT_STR_WIDTH(12), LCD_HEIGHT / 2 - 20, FONT_STR_WIDTH(23), 40); // lcd_fill
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
      } while ((status = ui_input_wait_release()) != 0);
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

// ----------------------------------------------------------------------------
// RTC

#ifdef __USE_RTC__
UI_FUNCTION_ADV_CALLBACK(menu_rtc_out_acb) {
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

// ----------------------------------------------------------------------------
// SYSTEM

const menuitem_t menu_system[] = {
    {MT_CALLBACK, MENU_CONFIG_TOUCH_CAL, "TOUCH CAL", menu_config_cb},
    {MT_CALLBACK, MENU_CONFIG_TOUCH_TEST, "TOUCH TEST", menu_config_cb},
#ifdef __LCD_BRIGHTNESS__
    {MT_ADV_CALLBACK, 0, "BRIGHTNESS\n " R_LINK_COLOR "%d%%%%", menu_brightness_acb},
#endif
    {MT_CALLBACK, MENU_CONFIG_SAVE, "SAVE CONFIG", menu_config_cb},
#if defined(__SD_CARD_LOAD__) && !defined(__SD_FILE_BROWSER__)
    {MT_CALLBACK, MENU_CONFIG_LOAD, "LOAD CONFIG", menu_config_cb},
#endif
    {MT_CALLBACK, MENU_CONFIG_VERSION, "VERSION", menu_config_cb},
#ifdef __USE_RTC__
    {MT_SUBMENU, 0, "DATE/TIME", menu_rtc},
#endif
    {MT_SUBMENU, 0, "DEVICE", menu_device},
#ifdef __USE_SERIAL_CONSOLE__
    #ifdef __USE_SERIAL_CONSOLE__
{MT_SUBMENU, 0, "CONNECTION", menu_connection},
#endif
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
