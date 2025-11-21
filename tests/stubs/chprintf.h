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
 * Minimal host stub for the ChibiOS chprintf helpers.  The production firmware
 * relies on chvprintf() for shell output; unit tests provide the implementation
 * in their translation unit so they can capture formatted strings.
 */

#pragma once

#include <stdarg.h>

typedef struct BaseSequentialStream BaseSequentialStream;

int chvprintf(BaseSequentialStream* chp, const char* fmt, va_list ap);
