/*
 * Centralized application feature toggles.
 *
 * Each macro defaults to enabled but can be overridden at compile time
 * by defining the symbol before including this header (for example via
 * compiler command line options).
 * 
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


#ifndef ENABLED_DUMP_COMMAND
#define ENABLED_DUMP_COMMAND 1
#endif

#ifndef ENABLE_SCANBIN_COMMAND
#define ENABLE_SCANBIN_COMMAND 1
#endif

#ifndef ENABLE_INFO_COMMAND
#define ENABLE_INFO_COMMAND 1
#endif

#ifndef ENABLE_USART_COMMAND
#define ENABLE_USART_COMMAND 1
#endif

#ifndef ENABLE_CONFIG_COMMAND
#define ENABLE_CONFIG_COMMAND 1
#endif

#ifndef ENABLE_THREADS_COMMAND
#define ENABLE_THREADS_COMMAND 0
#endif

#ifndef ENABLE_SAMPLE_COMMAND
#define ENABLE_SAMPLE_COMMAND 0
#endif

#ifndef ENABLE_I2C_COMMAND
#define ENABLE_I2C_COMMAND 0
#endif

#ifndef ENABLE_LCD_COMMAND
#define ENABLE_LCD_COMMAND 0
#endif

#ifndef ENABLE_TEST_COMMAND
#define ENABLE_TEST_COMMAND 0
#endif

#ifndef ENABLE_STAT_COMMAND
#define ENABLE_STAT_COMMAND 0
#endif

#ifndef ENABLE_GAIN_COMMAND
#define ENABLE_GAIN_COMMAND 0
#endif

#ifndef ENABLE_PORT_COMMAND
#define ENABLE_PORT_COMMAND 0
#endif

#ifndef ENABLE_BAND_COMMAND
#define ENABLE_BAND_COMMAND 0
#endif


// Enable output debug data on screen on hard fault
// #define ENABLE_HARD_FAULT_HANDLER_DEBUG
// Enable si5351 register write, used for debug
// #define ENABLE_SI5351_REG_WRITE
// Enable i2c timing command, used for debug
// #define ENABLE_I2C_TIMINGS
// Enable debug for console command
// #define DEBUG_CONSOLE_SHOW
