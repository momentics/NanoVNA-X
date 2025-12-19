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

#include "ch.h"
#include "hal.h"
#include "nanovna.h"
#include "ui/ui_menu.h"
#include "ui/core/ui_core.h" // For globals/ui_mode/etc if needed
#include "ui/core/ui_menu_engine.h" // For menu_dynamic_acquire, etc
#include "ui/core/ui_keypad.h" // For KM_ macros
#include "ui/menus/menu_stimulus.h"

// Forward decs
static const menuitem_t* menu_build_points_menu(void);

// Callbacks

static UI_FUNCTION_ADV_CALLBACK(menu_points_sel_acb) {
  (void)data;
  if (b) {
    b->p1.u = sweep_points;
    return;
  }
  menu_push_submenu(menu_build_points_menu());
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

// Descriptors and Dynamic Builder

static const menu_descriptor_t menu_points_desc[] = {
    {MT_ADV_CALLBACK, 0},
    {MT_ADV_CALLBACK, 1},
    {MT_ADV_CALLBACK, 2},
    {MT_ADV_CALLBACK, 3},
#if POINTS_SET_COUNT > 4
    {MT_ADV_CALLBACK, 4},
#endif
};

static const menuitem_t* menu_build_points_menu(void) {
  menuitem_t* cursor = menu_dynamic_acquire();
  const menuitem_t* base = cursor;
  *cursor++ = (menuitem_t){MT_ADV_CALLBACK,
                           KM_POINTS,
                           "SET POINTS\n " R_LINK_COLOR "%d",
                           (const void*)menu_keyboard_acb};
  cursor = ui_menu_list(menu_points_desc, ARRAY_COUNT(menu_points_desc), "%d point",
                        menu_points_acb, cursor);
  menu_set_next(cursor, menu_back);
  return base;
}

// Menu Definition

// Keyboard Callbacks
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

UI_KEYBOARD_CALLBACK(input_points) {
  (void)data;
  if (b) {
    b->p1.u = sweep_points;
    return;
  }
  set_sweep_points(keyboard_get_uint());
}

const menuitem_t menu_stimulus[] = {
    {MT_ADV_CALLBACK, KM_START, "START", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_STOP, "STOP", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_CENTER, "CENTER", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_SPAN, "SPAN", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_CW, "CW FREQ", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_STEP, "FREQ STEP\n " R_LINK_COLOR "%bF" S_Hz, menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_VAR, "JOG STEP\n " R_LINK_COLOR "AUTO", menu_keyboard_acb},
    {MT_ADV_CALLBACK, 0, "MORE PTS\n " R_LINK_COLOR "%u", menu_points_sel_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
