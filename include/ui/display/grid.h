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
