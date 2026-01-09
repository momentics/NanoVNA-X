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





#ifndef __UI_MENUS_MENU_DISPLAY_H__
#define __UI_MENUS_MENU_DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "ui/ui_menu.h"

extern const menuitem_t menu_display[];
extern const menuitem_t menu_measure_tools[];
void input_amplitude(uint16_t data, button_t* b);
void input_scale(uint16_t data, button_t* b);
void input_ref(uint16_t data, button_t* b);
void input_edelay(uint16_t data, button_t* b);
void input_s21_offset(uint16_t data, button_t* b);
void input_velocity(uint16_t data, button_t* b);
void input_cable_len(uint16_t data, button_t* b);
void input_measure_r(uint16_t data, button_t* b);
void input_portz(uint16_t data, button_t* b);
 // Exposed for menu_main.c

#ifdef __cplusplus
}
#endif

#endif // __UI_MENUS_MENU_DISPLAY_H__
