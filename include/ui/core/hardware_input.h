/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Originally written using elements from Dmitry (DiSlord) dislordlive@gmail.com
 * Originally written using elements from TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
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





#ifndef __UI_CORE_HARDWARE_INPUT_H__
#define __UI_CORE_HARDWARE_INPUT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "ch.h"
#include "hal.h"
#include "nanovna.h"

#define NO_EVENT 0
#define EVT_BUTTON_SINGLE_CLICK 0x01
#define EVT_BUTTON_DOUBLE_CLICK 0x02
#define EVT_BUTTON_DOWN_LONG 0x04
#define EVT_UP 0x10
#define EVT_DOWN 0x20
#define EVT_REPEAT 0x40

#define BUTTON_DOWN_LONG_TICKS MS2ST(500)
#define BUTTON_DOUBLE_TICKS MS2ST(250)
#define BUTTON_REPEAT_TICKS MS2ST(30)
#define BUTTON_DEBOUNCE_TICKS MS2ST(20)

#define BUTTON_DOWN (1 << GPIOA_LEVER1)
#define BUTTON_PUSH (1 << GPIOA_PUSH)
#define BUTTON_UP (1 << GPIOA_LEVER2)

uint16_t ui_input_get_buttons(void);
uint16_t ui_input_check(void);
uint16_t ui_input_wait_release(void);
void ui_input_reset_state(void);

#ifdef __cplusplus
}
#endif

#endif // __UI_CORE_HARDWARE_INPUT_H__
