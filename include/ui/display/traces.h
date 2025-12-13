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

#ifndef UI_DISPLAY_TRACES_H
#define UI_DISPLAY_TRACES_H

#include "ui/display/plot_internal.h"

// Trace data cache types
#if HEIGHT > UINT8_MAX
typedef uint16_t trace_coord_t;
#else
typedef uint8_t trace_coord_t;
#endif

typedef struct {
  uint16_t* x;
  trace_coord_t* y;
} trace_index_table_t;

typedef struct {
  const uint16_t* x;
  const trace_coord_t* y;
} trace_index_const_table_t;

// Extern data for inline access
extern uint16_t trace_index_x[TRACE_INDEX_COUNT][SWEEP_POINTS_MAX];
extern trace_coord_t trace_index_y[TRACE_INDEX_COUNT][SWEEP_POINTS_MAX];

// Accessors
static inline trace_index_table_t trace_index_table(int trace_id) {
  trace_index_table_t table = {trace_index_x[trace_id], trace_index_y[trace_id]};
  return table;
}

static inline trace_index_const_table_t trace_index_const_table(int trace_id) {
  trace_index_const_table_t table = {trace_index_x[trace_id], trace_index_y[trace_id]};
  return table;
}

#define TRACE_X(table, idx) ((table).x[(idx)])
#define TRACE_Y(table, idx) ((table).y[(idx)])

// Function declarations
uint32_t gather_trace_mask(bool* smith_is_impedance);

void trace_print_value_string(RenderCellCtx* rcx, int xpos, int ypos, int t, int index,
                              int index_ref);

// Measurement callbacks
float logmag(int i, const float* v);
float phase(int i, const float* v);
float groupdelay(const float* v, const float* w, uint32_t deltaf);
float groupdelay_from_array(int i, const float* v);
float real(int i, const float* v);
float imag(int i, const float* v);
float linear(int i, const float* v);
float swr(int i, const float* v);
float resistance(int i, const float* v);
float reactance(int i, const float* v);
float mod_z(int i, const float* v);
float phase_z(int i, const float* v);
float qualityfactor(int i, const float* v);
float susceptance(int i, const float* v);
float conductance(int i, const float* v);
float parallel_r(int i, const float* v);
float parallel_x(int i, const float* v);
float parallel_c(int i, const float* v);
float parallel_l(int i, const float* v);
float mod_y(int i, const float* v);
float s21shunt_r(int i, const float* v);
float s21shunt_x(int i, const float* v);
float s21shunt_z(int i, const float* v);
float s21series_r(int i, const float* v);
float s21series_x(int i, const float* v);
float s21series_z(int i, const float* v);
float s21_qualityfactor(int i, const float* v);
float series_c_impl(int i, const float* v);
float series_l_impl(int i, const float* v);

// Public API
const char* get_trace_typename(int t, int marker_smith_format);
const char* get_smith_format_names(int m);
void format_smith_value(RenderCellCtx* rcx, int xpos, int ypos, const float* coeff, uint16_t idx,
                        uint16_t m);
int trace_print_info(RenderCellCtx* rcx, int xpos, int ypos, int t);


float time_of_index(int idx);
float distance_of_index(int idx);

TraceIndexRange search_index_range_x(uint16_t x_start, uint16_t x_end,
                                     trace_index_const_table_t index);

void toggle_stored_trace(int idx);
uint8_t get_stored_traces(void);
bool need_process_trace(uint16_t idx);

void trace_into_index(int t);
void render_traces_in_cell(RenderCellCtx* rcx);

#endif // UI_DISPLAY_TRACES_H
