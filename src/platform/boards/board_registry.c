/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
 * Based on TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
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

#include "platform/boards/board_registry.h"

#if defined(NANOVNA_F303)
extern const platform_drivers_t *platform_nanovna_f303_drivers(void);
#define ACTIVE_DRIVERS platform_nanovna_f303_drivers()
#else
extern const platform_drivers_t *platform_nanovna_f072_drivers(void);
#define ACTIVE_DRIVERS platform_nanovna_f072_drivers()
#endif

void platform_board_pre_init(void) {}

const platform_drivers_t *platform_board_drivers(void) {
  return ACTIVE_DRIVERS;
}
