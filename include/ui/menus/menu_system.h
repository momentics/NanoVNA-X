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

extern const menuitem_t menu_system[];
UI_FUNCTION_ADV_CALLBACK(menu_offset_sel_acb);
UI_FUNCTION_ADV_CALLBACK(menu_vna_mode_acb);

void menu_sys_info(void);
void lcd_set_brightness(uint16_t b);

void input_xtal(uint16_t data, button_t* b);
void input_harmonic(uint16_t data, button_t* b);
void input_vbat(uint16_t data, button_t* b);
void input_date_time(uint16_t data, button_t* b);
void input_rtc_cal(uint16_t data, button_t* b);
