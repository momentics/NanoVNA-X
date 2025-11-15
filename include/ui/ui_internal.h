/*
 * Internal UI definitions shared across UI modules.
 *
 * Copyright (c) 2024, @momentics
 */

#pragma once

#include "nanovna.h"

#include <stdint.h>

// Touch screen events
#define EVT_TOUCH_NONE 0
#define EVT_TOUCH_DOWN 1
#define EVT_TOUCH_PRESSED 2
#define EVT_TOUCH_RELEASED 3

// Menu item types
enum {
  MT_NEXT = 0,   // reference is next menu or 0 if end
  MT_SUBMENU,    // reference is submenu button
  MT_CALLBACK,   // reference is pointer to: void ui_function_name(uint16_t data)
  MT_ADV_CALLBACK // reference is pointer to: void ui_function_name(uint16_t data, button_t* b)
};

// Button definition (used in MT_ADV_CALLBACK for custom)
#define BUTTON_ICON_NONE -1
#define BUTTON_ICON_NOCHECK 0
#define BUTTON_ICON_CHECK 1
#define BUTTON_ICON_GROUP 2
#define BUTTON_ICON_GROUP_CHECKED 3
#define BUTTON_ICON_CHECK_AUTO 4
#define BUTTON_ICON_CHECK_MANUAL 5

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
  } p1;
  char label[32];
} button_t;

typedef void (*menuaction_cb_t)(uint16_t data);
#define UI_FUNCTION_CALLBACK(ui_function_name) void ui_function_name(uint16_t data)

typedef void (*menuaction_acb_t)(uint16_t data, button_t* b);
#define UI_FUNCTION_ADV_CALLBACK(ui_function_name) void ui_function_name(uint16_t data, button_t* b)

typedef struct {
  uint8_t type;
  uint8_t data;
  const char* label;
  const void* reference;
} __attribute__((packed)) menuitem_t;

void ui_draw_button(uint16_t x, uint16_t y, uint16_t w, uint16_t h, button_t* b);
int touch_check(void);
void touch_wait_release(void);
void touch_position(int* x, int* y);
void ui_mode_normal(void);
