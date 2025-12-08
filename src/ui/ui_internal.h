#pragma once

#include "nanovna.h"

// UI Modes
enum {
  UI_NORMAL,
  UI_MENU,
  UI_KEYPAD,
#ifdef __SD_FILE_BROWSER__
  UI_BROWSER,
#endif
};

// Button structure
typedef struct {
  uint8_t bg;
  uint8_t fg;
  uint8_t border;
  int8_t icon;
  union {
    int32_t i;
    uint32_t u;
    float f;
    const char* text;
  } p1; // void data for label printf
  char label[32];
} button_t;

// Menu structures
enum {
  MT_NEXT = 0,    // reference is next menu or 0 if end
  MT_SUBMENU,     // reference is submenu button
  MT_CALLBACK,    // reference is pointer to: void ui_function_name(uint16_t data)
  MT_ADV_CALLBACK // reference is pointer to: void ui_function_name(uint16_t data, button_t *b)
};

// Button definition (used in MT_ADV_CALLBACK for custom)
#define BUTTON_ICON_NONE -1
#define BUTTON_ICON_NOCHECK 0
#define BUTTON_ICON_CHECK 1
#define BUTTON_ICON_GROUP 2
#define BUTTON_ICON_GROUP_CHECKED 3
#define BUTTON_ICON_CHECK_AUTO 4
#define BUTTON_ICON_CHECK_MANUAL 5
#define BUTTON_ICON_PAUSE 6
#define BUTTON_ICON_PLAY 7

#define BUTTON_BORDER_WIDTH_MASK 0x07

// Define mask for draw border (if 1 use light color, if 0 dark)
#define BUTTON_BORDER_NO_FILL 0x08
#define BUTTON_BORDER_TOP 0x10
#define BUTTON_BORDER_BOTTOM 0x20
#define BUTTON_BORDER_LEFT 0x40
#define BUTTON_BORDER_RIGHT 0x80

#define BUTTON_BORDER_FLAT 0x00
#define BUTTON_BORDER_RISE (BUTTON_BORDER_TOP | BUTTON_BORDER_RIGHT)
#define BUTTON_BORDER_FALLING (BUTTON_BORDER_BOTTOM | BUTTON_BORDER_LEFT)

// Call back functions for MT_CALLBACK type
typedef void (*menuaction_cb_t)(uint16_t data);
#define UI_FUNCTION_CALLBACK(ui_function_name) void ui_function_name(uint16_t data)

typedef void (*menuaction_acb_t)(uint16_t data, button_t* b);
#define UI_FUNCTION_ADV_CALLBACK(ui_function_name) void ui_function_name(uint16_t data, button_t* b)

void ui_mode_normal(void);

// Set structure align as WORD (save flash memory)
typedef struct {
  uint8_t type;
  uint8_t data;
  const char* label;
  const void* reference;
} __attribute__((packed)) menuitem_t;

typedef struct {
  uint8_t type;
  uint8_t data;
} menu_descriptor_t;

typedef struct {
  uint16_t value;
  const char* label;
  int8_t icon;
} option_desc_t;

// Keypad structures
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

// Request flags
#define UI_CONTROLLER_REQUEST_NONE    0x00
#define UI_CONTROLLER_REQUEST_LEVER   0x01
#define UI_CONTROLLER_REQUEST_TOUCH   0x02
#define UI_CONTROLLER_REQUEST_CONSOLE 0x04

// Provide extern access to these for split files if needed
extern uint8_t ui_mode;
extern int8_t selection;


void ui_save_file(char* name, uint8_t format);
void ui_draw_button(uint16_t x, uint16_t y, uint16_t w, uint16_t h, button_t* b);
int get_lines_count(const char* label);

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
