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

#ifndef ENABLE_USART_COMMAND
#define ENABLE_USART_COMMAND 1
#endif

#ifndef ENABLE_CONFIG_COMMAND
#define ENABLE_CONFIG_COMMAND 1
#endif
