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

#include "ui/ui_menu.h"

// Core Menu Engine API
void menu_stack_reset(void);
void menu_draw(uint32_t mask);
void menu_invoke(int item);

// Handlers for menu interaction
void ui_menu_lever(uint16_t status);
void ui_menu_touch(int touch_x, int touch_y);

// Helpers
void menu_move_back(bool leave_ui);
void menu_push_submenu(const menuitem_t* submenu);
void menu_set_submenu(const menuitem_t* submenu);

// Exposed for ui_controller.c to build menus
const menuitem_t* current_menu_item(int i);
int current_menu_get_count(void);
void ui_mode_menu(void);

extern const menuitem_t menu_back[];

// Generic Menu Callbacks
UI_FUNCTION_ADV_CALLBACK(menu_keyboard_acb);

// Dynamic Menu Buffer (for dynamic construction)
menuitem_t* menu_dynamic_acquire(void);

// Exposed globals (from engine)
extern int8_t selection;
extern uint8_t menu_current_level;
