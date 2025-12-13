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

/*
 * UI icon resource declarations.
 */

#pragma once

#include <stdint.h>

#include "nanovna.h"

#if LCD_WIDTH < 800
#define CALIBRATION_OFFSET 16
#define TOUCH_MARK_W 9
#define TOUCH_MARK_H 9
#define TOUCH_MARK_X 4
#define TOUCH_MARK_Y 4
#else
#define CALIBRATION_OFFSET 16
#define TOUCH_MARK_W 15
#define TOUCH_MARK_H 15
#define TOUCH_MARK_X 7
#define TOUCH_MARK_Y 7
#endif

extern const uint8_t touch_bitmap[];

#if _USE_FONT_ < 3
#define ICON_SIZE 14
#define ICON_WIDTH 11
#define ICON_HEIGHT 11
#else
#define ICON_SIZE 18
#define ICON_WIDTH 14
#define ICON_HEIGHT 14
#endif

extern const uint8_t button_icons[];
#define ICON_GET_DATA(i) (&button_icons[2 * ICON_HEIGHT * (i)])

