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





#ifndef __UI_DRAW_RENDER_H__
#define __UI_DRAW_RENDER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "ui/draw/plot_internal.h"
#include <stdarg.h>

void cell_drawline(const RenderCellCtx* rcx, int x0, int y0, int x1, int y1, pixel_t c);
void cell_blit_bitmap(RenderCellCtx* rcx, int16_t x, int16_t y, uint16_t w, uint16_t h,
                             const uint8_t* bmp);

#if VNA_ENABLE_SHADOW_TEXT
void cell_blit_bitmap_shadow(RenderCellCtx* rcx, int16_t x, int16_t y, uint16_t w, uint16_t h,
                                    const uint8_t* bmp);
#endif

void cell_set_font(int type);
int cell_printf_ctx(RenderCellCtx* rcx, int16_t x, int16_t y, const char* fmt, ...);
int cell_printf_bound(int16_t x, int16_t y, const char* fmt, ...);

void set_active_cell_ctx(RenderCellCtx* ctx);

#ifdef __cplusplus
}
#endif

#endif // __UI_DRAW_RENDER_H__
