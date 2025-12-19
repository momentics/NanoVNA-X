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

#ifndef UI_DISPLAY_GRID_H
#define UI_DISPLAY_GRID_H

#include "ui/display/plot_internal.h"

void update_grid(freq_t fstart, freq_t fstop);
void render_polar_grid_cell(const RenderCellCtx* rcx, pixel_t color);
void render_smith_grid_cell(const RenderCellCtx* rcx, pixel_t color);
void render_admittance_grid_cell(const RenderCellCtx* rcx, pixel_t color);

int rectangular_grid_x(uint32_t x);
int rectangular_grid_y(uint32_t y);

void render_rectangular_grid_layer(RenderCellCtx* rcx, pixel_t color);
void render_round_grid_layer(RenderCellCtx* rcx, pixel_t color, uint32_t trace_mask, bool smith_impedance);

#endif // UI_DISPLAY_GRID_H
