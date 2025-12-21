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

#include "core/data_types.h"
#include "ui/ui_style.h"

extern alignas(8) float measured[2][SWEEP_POINTS_MAX][2];

extern const trace_info_t trace_info_list[MAX_TRACE_TYPE];
extern const marker_info_t marker_info_list[MS_END];

extern config_t config;
extern properties_t current_props;


extern const char* const info_about[];
extern volatile bool calibration_in_progress;

extern pixel_t foreground_color;
extern pixel_t background_color;

extern alignas(8) pixel_t spi_buffer[SPI_BUFFER_SIZE];

extern uint16_t lastsaveid;
