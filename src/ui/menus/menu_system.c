#include "ch.h"
#include "hal.h"
#include "nanovna.h"
#include "infra/state/state_manager.h"
#include "platform/peripherals/si5351.h"
#include "ui/ui_menu.h"
#include "ui/core/ui_core.h"
#include "ui/core/ui_menu_engine.h"
#include "ui/core/ui_keypad.h"
#include "ui/menus/menu_system.h"
#include "infra/storage/config_service.h"
#include "infra/event/event_bus.h"
#include <string.h>

// Enums for config callback
enum {
  MENU_CONFIG_TOUCH_CAL,
  MENU_CONFIG_TOUCH_TEST,
  MENU_CONFIG_VERSION,
  MENU_CONFIG_SAVE,
  MENU_CONFIG_RESET,
#if defined(__SD_CARD_LOAD__) && !defined(__SD_FILE_BROWSER__)
  MENU_CONFIG_LOAD,
#endif
};

extern const char* const info_about[];

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
  lcd_drawstring(x, y += str_height, __DATE__ " " __TIME__);
//lcd_drawstring(x, y += str_height, USING_OS_NAME);
//lcd_drawstring(x, y += str_height, MCU_NAME);

  while (ui_input_wait_release() == 0)
    chThdSleepMilliseconds(100);
  while (ui_input_check() == 0)
    chThdSleepMilliseconds(100);
  while (ui_input_wait_release() != 0)
    chThdSleepMilliseconds(100);
  request_to_redraw(REDRAW_ALL);
}

// ===================================
// VNA Mode Logic
// ===================================
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

// ===================================
// Serial Speed Logic
// ===================================
#ifdef __USE_SERIAL_CONSOLE__
static const menu_descriptor_t menu_serial_speed_desc[] = {
    {MT_ADV_CALLBACK, 0},
    {MT_ADV_CALLBACK, 1},
    {MT_ADV_CALLBACK, 2},
    {MT_ADV_CALLBACK, 3},
    {MT_ADV_CALLBACK, 4},
    {MT_ADV_CALLBACK, 5},
    {MT_ADV_CALLBACK, 6},
    {MT_ADV_CALLBACK, 7},
    {MT_ADV_CALLBACK, 8},
    {MT_ADV_CALLBACK, 9},
};

static UI_FUNCTION_ADV_CALLBACK(menu_serial_speed_acb) {
  if (b) {
    uint32_t speed = shell_speed(data);
    b->icon = shell_get_speed_index() == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = speed;
    return;
  }
  shell_set_speed(data);
}

static UI_FUNCTION_ADV_CALLBACK(menu_serial_speed_sel_acb) {
  if (b) {
    b->p1.u = shell_speed(shell_get_speed_index());
    return;
  }
  menuitem_t* cursor = menu_dynamic_acquire();
  menu_push_submenu(cursor);
  cursor = ui_menu_list(menu_serial_speed_desc, ARRAY_COUNT(menu_serial_speed_desc), "%u",
                        menu_serial_speed_acb, cursor);
  menu_set_next(cursor, menu_back);
}
#endif

// Callbacks

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
static UI_FUNCTION_CALLBACK(menu_dfu_cb) {
  (void)data;
  ui_enter_dfu();
}
#endif

// Offset menu logic
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

const menuitem_t menu_offset[]; // Forward declaration
UI_FUNCTION_ADV_CALLBACK(menu_offset_sel_acb) {
  (void)data;
  if (b) {
    b->p1.i = IF_OFFSET;
    return;
  }
  menu_push_submenu(menu_offset);
}
#endif

// Brightness logic
#ifdef __LCD_BRIGHTNESS__
static void lcd_set_brightness(uint16_t b) {
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

// Band Mode Logic
// Band Mode Logic
static const option_desc_t band_mode_options[] = {
    {0, "Direct", BUTTON_ICON_NONE},
    {1, "Div", BUTTON_ICON_NONE},
    {2, "Multi", BUTTON_ICON_NONE},
};


static UI_FUNCTION_ADV_CALLBACK(menu_band_sel_acb) {
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

// RTC Logic
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
#endif

// Menus

#ifdef __DFU_SOFTWARE_MODE__
const menuitem_t menu_dfu[] = {
    {MT_CALLBACK, 0, "RESET AND\nENTER DFU", menu_dfu_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

#ifdef __USE_SERIAL_CONSOLE__
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
    {MT_NEXT, 0, NULL, menu_back}
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
const menuitem_t menu_rtc[] = {
    {MT_ADV_CALLBACK, KM_RTC_DATE, "SET DATE", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_RTC_TIME, "SET TIME", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_RTC_CAL, "RTC CAL\n " R_LINK_COLOR "%+b.3f" S_PPM, menu_keyboard_acb},
    {MT_ADV_CALLBACK, 0, "RTC 512" S_Hz "\n Led2 %s", menu_rtc_out_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

// Keyboard Callbacks
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
