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

#ifndef _UI_MENUS_MENU_STORAGE_H_
#define _UI_MENUS_MENU_STORAGE_H_

#include "ui/ui_menu.h"

// Expose the SD Card menu for usage in menu_main.c
extern const menuitem_t menu_sdcard[];

#ifdef __SD_FILE_BROWSER__
extern const menuitem_t menu_sdcard_browse[];
#endif

void input_filename(uint16_t data, button_t* b);

#endif // _UI_MENUS_MENU_STORAGE_H_
