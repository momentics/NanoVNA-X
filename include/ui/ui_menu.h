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
  } p1; // void data for label printf
  char label[32];
} button_t;

// Call back functions for MT_CALLBACK type
typedef void (*menuaction_cb_t)(uint16_t data);
#define UI_FUNCTION_CALLBACK(ui_function_name) void ui_function_name(uint16_t data)

typedef void (*menuaction_acb_t)(uint16_t data, button_t* b);
#define UI_FUNCTION_ADV_CALLBACK(ui_function_name) void ui_function_name(uint16_t data, button_t* b)

// Set structure align as WORD (save flash memory)
typedef struct {
  uint8_t type;
  uint8_t data;
  const char* label;
  const void* reference;
} __attribute__((packed)) menuitem_t;

// Type of menu item:
enum {
  MT_NEXT = 0,    // reference is next menu or 0 if end
  MT_SUBMENU,     // reference is submenu button
  MT_CALLBACK,    // reference is pointer to: void ui_function_name(uint16_t data)
  MT_ADV_CALLBACK // reference is pointer to: void ui_function_name(uint16_t data, button_t *b)
};

typedef struct {
  uint8_t type;
  uint8_t data;
} menu_descriptor_t;

typedef struct {
  uint16_t value;
  const char* label;
  int8_t icon;
} option_desc_t;

menuitem_t* ui_menu_list(const menu_descriptor_t* descriptors, size_t count,
                                const char* label, const void* reference,
                                menuitem_t* out);

void menu_set_next(menuitem_t* entry, const menuitem_t* next);
void ui_cycle_option(uint16_t* dst, const option_desc_t* list, size_t count, button_t* b);
