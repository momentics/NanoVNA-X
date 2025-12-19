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

#include <stdbool.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ch.h"
#include "hal.h"
#include "chprintf.h"
#include "nanovna.h"
#include "infra/state/state_manager.h"

// Icons bitmap resources
// Icons bitmap resources
#include "../resources/icons/icons_marker.c"
#include "ui/display/plot_internal.h"
#include "ui/display/grid.h"
#include "ui/display/render.h"
#include "ui/display/traces.h"

/*
 * NanoVNA-X plot module
 * ---------------------
 * This module renders grids, traces, markers, and UI decorations directly into the
 * LCD DMA cell buffer used by the SPI/I²S display controller. Rendering is
 * organized around small tiles ("cells") that can be invalidated individually to
 * keep updates responsive even on constrained MCUs.
 *
 * Feature flags:
 *   VNA_FAST_RENDER    - Use the optimized renderer located in
 *                        src/ui/display/fast_render/vna_render.c (defaults to the legacy
 *                        implementation when undefined).
 *   VNA_ENABLE_SHADOW_TEXT - Enables drop shadow text rendering when true. The
 *                        legacy configuration macro _USE_SHADOW_TEXT_ still
 *                        works and feeds this flag.
 *   VNA_ENABLE_GRID_VALUES - Enables textual grid value annotations. This flag
 *                        replaces ad-hoc #if 0/1 toggles.
 *
 * Behaviour of the public API (draw_all(), request_to_redraw(), plot_init(),
 * etc.) is preserved. All helper functions are kept static to avoid leaking
 * internal symbols.
 */

#ifndef VNA_FAST_RENDER
#ifdef __VNA_FAST_RENDER__
#define VNA_FAST_RENDER 1
#else
#define VNA_FAST_RENDER 0
#endif
#endif

#ifndef VNA_ENABLE_SHADOW_TEXT
#ifdef _USE_SHADOW_TEXT_
#define VNA_ENABLE_SHADOW_TEXT 1
#else
#define VNA_ENABLE_SHADOW_TEXT 0
#endif
#endif

#ifndef VNA_ENABLE_GRID_VALUES
#ifdef __USE_GRID_VALUES__
#define VNA_ENABLE_GRID_VALUES 1
#else
#define VNA_ENABLE_GRID_VALUES 0
#endif
#endif

static uint16_t redraw_request = 0; // contains REDRAW_XXX flags

static uint16_t area_width;
static uint16_t area_height;

static uint16_t last_plotted_sweep_points;
static uint32_t last_plotted_trace_mask;

// Mark cell to update here
map_t markmap[MAX_MARKMAP_Y];

// Trace data cache, for faster redraw cells
#define TRACE_INDEX_COUNT (TRACES_MAX + STORED_TRACES)
_Static_assert(TRACE_INDEX_COUNT > 0, "Trace index count must be positive");

// Trace data cache types and arrays moved to traces.c/h

// Forward declarations moved or resolved

// render_traces_in_cell implementation needs to stay or move?
// I implemented it in traces.c? No, I copied it.
// I should check traces.c content.
// I did NOT put render_traces_in_cell in traces.c in step 499?
// I copied "trace_into_index".
// Did I copy "render_traces_in_cell"?
// Let's check my input for step 499.
// I see "void render_traces_in_cell(RenderCellCtx* rcx) {" in my memory? 
// No, I see "void trace_into_index(int t) {...}"
// I see "mark_line", "mark_set_index", trace info lists...
// I do NOT see "render_traces_in_cell" in the written traces.c content in step 499.
// I missed it?
// Wait, plan said "render_traces_in_cell (exported? used by plot.c)".
// If I missed it, I should keep it in plot.c for now or add it to traces.c.
// Ideally it belongs in traces.c.
// If I remove it from plot.c now, it will be missing.
// I will check if I added it.
// If not, I will add it to traces.c first.

// render_traces_in_cell moved to traces.c

/**
 * @brief Draw marker icons for every enabled trace.
 */
static void render_markers_in_cell(RenderCellCtx* rcx) {
  for (int i = 0; i < MARKERS_MAX; i++) {
    if (!markers[i].enabled)
      continue;
    int mk_idx = markers[i].index;
    for (int t = 0; t < TRACES_MAX; t++) {
      if (!trace[t].enabled)
        continue;
      trace_index_const_table_t index = trace_index_const_table(t);
      const uint8_t* plate;
      const uint8_t* marker;
      int x = TRACE_X(index, mk_idx) - rcx->x0 - X_MARKER_OFFSET;
      int y;
      if (TRACE_Y(index, mk_idx) < MARKER_HEIGHT * 2) {
        y = TRACE_Y(index, mk_idx) - rcx->y0 + 1;
        plate = MARKER_RBITMAP(0);
        marker = MARKER_RBITMAP(i + 1);
      } else {
        y = TRACE_Y(index, mk_idx) - rcx->y0 - Y_MARKER_OFFSET;
        plate = MARKER_BITMAP(0);
        marker = MARKER_BITMAP(i + 1);
      }
      if ((uint32_t)(x + MARKER_WIDTH) < (CELLWIDTH + MARKER_WIDTH) &&
          (uint32_t)(y + MARKER_HEIGHT) < (CELLHEIGHT + MARKER_HEIGHT)) {
        lcd_set_foreground(LCD_TRACE_1_COLOR + t);
        cell_blit_bitmap(rcx, x, y, MARKER_WIDTH, MARKER_HEIGHT, plate);
        lcd_set_foreground(LCD_TXT_SHADOW_COLOR);
        cell_blit_bitmap(rcx, x, y, MARKER_WIDTH, MARKER_HEIGHT, marker);
      }
    }
  }
}





//**************************************************************************************
// Cell render functions used to be here (moved to render.c)
//**************************************************************************************
// See ui/display/render.h

//**************************************************************************************
// Cell mark map functions
//**************************************************************************************
// mark_line and mark_set_index moved to traces.c

static inline void clear_markmap(void) {
  int n = MAX_MARKMAP_Y - 1;
  do {
    markmap[n] = (map_t)0;
  } while (n--);
}

/*
 * Force full screen update
 */
static inline void force_set_markmap(void) {
  int n = MAX_MARKMAP_Y - 1;
  do {
    markmap[n] = (map_t)-1;
  } while (n--);
}

/*
 * Force region of screen update
 */
/**
 * @brief Mark all cells intersecting the pixel rectangle as dirty.
 */
static inline void invalidate_rect_px(int x0, int y0, int x1, int y1) {
  if (x0 > x1)
    SWAP(int, x0, x1);
  if (y0 > y1)
    SWAP(int, y0, y1);
  x0 = clamp_u16(x0 / CELLWIDTH, 0, MAX_MARKMAP_X - 1);
  x1 = clamp_u16(x1 / CELLWIDTH, 0, MAX_MARKMAP_X - 1);
  y0 = clamp_u16(y0 / CELLHEIGHT, 0, MAX_MARKMAP_Y - 1);
  y1 = clamp_u16(y1 / CELLHEIGHT, 0, MAX_MARKMAP_Y - 1);
  map_t mask = markmap_mask((uint16_t)x0, (uint16_t)x1);
  for (int y = y0; y <= y1; ++y)
    markmap[y] |= mask;
}

//**************************************************************************************
// NanoVNA measures
// This functions used for plot traces, and markers data output
// Also can used in measure calculations
//**************************************************************************************
// PORT_Z moved to plot_internal.h
// Math helpers moved to plot_internal.h

//**************************************************************************************
// LINEAR = |S|
//**************************************************************************************
// dummy compact_cell_buffer
static inline void compact_cell_buffer(RenderCellCtx* rcx) {
  if (rcx->w == CELLWIDTH) return;
  pixel_t* buf = rcx->buf;
  for (int y = 1; y < rcx->h; y++) {
    memmove(buf + y * rcx->w, buf + y * CELLWIDTH, rcx->w * sizeof(pixel_t));
  }
}

// Measurement callbacks moved to traces.c

// cartesian_scale moved to traces.c

// trace_info_list and marker_info_list moved to traces.c

// Trace printing and helper functions moved to traces.c

//**************************************************************************************
// Give a little speedup then draw rectangular plot
// Write more difficult algorithm for search indexes not give speedup
//**************************************************************************************
/**
 * @brief Locate sweep point bounds intersecting the horizontal span.
 *
 * Returns the first and last indices whose x-coordinates fall within
 * [x_start, x_end). The caller extends the range by one sample on each side
 * to maintain continuous segments.
 */
// search_index_range_x moved to traces.c

//**************************************************************************************
//                  Marker text/marker plate functions
//**************************************************************************************
void request_to_draw_marker(uint16_t mk_idx) {
  for (int t = 0; t < TRACES_MAX; t++) {
    if (!trace[t].enabled)
      continue;
    trace_index_const_table_t index = trace_index_const_table(t);
    int x = TRACE_X(index, mk_idx) - X_MARKER_OFFSET;
    int y = TRACE_Y(index, mk_idx) +
            ((TRACE_Y(index, mk_idx) < MARKER_HEIGHT * 2) ? 1 : -Y_MARKER_OFFSET);
    invalidate_rect_px(x, y, x + MARKER_WIDTH - 1, y + MARKER_HEIGHT - 1);
  }
}

// Calculate marker area size depend from trace/marker count and options
static int marker_area_max(void) {
  int t_count = 0, m_count = 0, i;
  for (i = 0; i < TRACES_MAX; i++)
    if (trace[i].enabled)
      t_count++;
  for (i = 0; i < MARKERS_MAX; i++)
    if (markers[i].enabled)
      m_count++;
  int cnt = t_count > m_count ? t_count : m_count;
  int extra = 0;
  if (get_electrical_delay() != 0.0f)
    extra += 2;
  if (s21_offset != 0.0f)
    extra += 2;
#ifdef __VNA_Z_RENORMALIZATION__
  if (current_props._portz != 50.0f)
    extra += 2;
#endif
  if (extra < 2)
    extra = 2;
  cnt = (cnt + extra + 1) >> 1;
  return cnt * FONT_STR_HEIGHT;
}

static inline void markmap_marker_area(void) {
  // Hardcoded, Text info from upper area
  invalidate_rect_px(0, 0, AREA_WIDTH_NORMAL, marker_area_max());
}

static void markmap_all_markers(void) {
  int i;
  for (i = 0; i < MARKERS_MAX; i++) {
    if (!markers[i].enabled)
      continue;
    request_to_draw_marker(markers[i].index);
  }
  markmap_marker_area();
}

//**************************************************************************************
//            Marker search functions
//**************************************************************************************
static bool _greater(int x, int y) {
  return x > y;
}
static bool _lesser(int x, int y) {
  return x < y;
}

void marker_search(void) {
  int i, value;
  int found = 0;
  if (current_trace == TRACE_INVALID || active_marker == MARKER_INVALID)
    return;
  // Select search index table
  trace_index_const_table_t index = trace_index_const_table(current_trace);
  // Select compare function (depend from config settings)
  bool (*compare)(int x, int y) = VNA_MODE(VNA_MODE_SEARCH) ? _lesser : _greater;
  for (i = 1, value = TRACE_Y(index, 0); i < sweep_points; i++) {
    if ((*compare)(value, TRACE_Y(index, i))) {
      value = TRACE_Y(index, i);
      found = i;
    }
  }
  set_marker_index(active_marker, found);
}

void marker_search_dir(int16_t from, int16_t dir) {
  int i, value;
  int found = -1;
  if (current_trace == TRACE_INVALID || active_marker == MARKER_INVALID)
    return;
  // Select search index table
  trace_index_const_table_t index = trace_index_const_table(current_trace);
  // Select compare function (depend from config settings)
  bool (*compare)(int x, int y) = VNA_MODE(VNA_MODE_SEARCH) ? _lesser : _greater;
  // Search next
  for (i = from + dir, value = TRACE_Y(index, from); i >= 0 && i < sweep_points; i += dir) {
    if ((*compare)(value, TRACE_Y(index, i)))
      break;
    value = TRACE_Y(index, i);
  }
  //
  for (; i >= 0 && i < sweep_points; i += dir) {
    if ((*compare)(TRACE_Y(index, i), value))
      break;
    value = TRACE_Y(index, i);
    found = i;
  }
  if (found < 0)
    return;
  set_marker_index(active_marker, found);
}

int distance_to_index(int8_t t, uint16_t idx, int16_t x, int16_t y) {
  trace_index_const_table_t index = trace_index_const_table(t);
  x -= (int16_t)TRACE_X(index, idx);
  y -= (int16_t)TRACE_Y(index, idx);
  return x * x + y * y;
}

int search_nearest_index(int x, int y, int t) {
  int min_i = -1;
  int min_d = MARKER_PICKUP_DISTANCE * MARKER_PICKUP_DISTANCE;
  int i;
  for (i = 0; i < sweep_points; i++) {
    int d = distance_to_index(t, i, x, y);
    if (d >= min_d)
      continue;
    min_d = d;
    min_i = i;
  }
  return min_i;
}

//**************************************************************************************
//            Reference plate draw and update
//**************************************************************************************
static void markmap_all_refpos(void) {
  // Hardcoded, reference marker plates
  invalidate_rect_px(0, 0, CELLOFFSETX + 1, AREA_HEIGHT_NORMAL);
}

static void cell_draw_all_refpos(RenderCellCtx* rcx) {
  int x = -((int)rcx->x0) + CELLOFFSETX - REFERENCE_X_OFFSET;
  if ((uint32_t)(x + REFERENCE_WIDTH) >= CELLWIDTH + REFERENCE_WIDTH)
    return;
  for (int t = 0; t < TRACES_MAX; t++) {
    // Skip draw reference position for disabled/smith/polar traces
    if (!trace[t].enabled || ((1 << trace[t].type) & (ROUND_GRID_MASK)))
      continue;
    int y = HEIGHT - float2int(get_trace_refpos(t) * GRIDY) - rcx->y0 - REFERENCE_Y_OFFSET;
    if ((uint32_t)(y + REFERENCE_HEIGHT) < CELLHEIGHT + REFERENCE_HEIGHT) {
      lcd_set_foreground(LCD_TRACE_1_COLOR + t);
      cell_blit_bitmap(rcx, x, y, REFERENCE_WIDTH, REFERENCE_HEIGHT, (const uint8_t*)reference_bitmap);
    }
  }
}

//**************************************************************************************
//            Update cells behind menu
//**************************************************************************************
void request_to_draw_cells_behind_menu(void) {
  // Values Hardcoded from ui.c
  invalidate_rect_px(LCD_WIDTH - MENU_BUTTON_WIDTH - OFFSETX, 0, LCD_WIDTH - OFFSETX,
                    LCD_HEIGHT - 1);
  request_to_redraw(REDRAW_CELLS | REDRAW_FREQUENCY);
}

//**************************************************************************************
//            Measure module draw results and calculations
//**************************************************************************************
#ifdef __VNA_MEASURE_MODULE__
typedef void (*measure_cell_cb_t)(int x0, int y0);
typedef void (*measure_prepare_cb_t)(uint8_t mode, uint8_t update_mask);

static uint8_t data_update = 0;

#define MESAURE_NONE 0
#define MESAURE_S11 1                           // For calculate need only S11 data
#define MESAURE_S21 2                           // For calculate need only S21 data
#define MESAURE_ALL (MESAURE_S11 | MESAURE_S21) // For calculate need S11 and S21 data

#define MEASURE_UPD_SWEEP (1 << 0) // Recalculate on sweep done
#define MEASURE_UPD_FREQ (1 << 1)  // Recalculate on marker change position
#define MEASURE_UPD_ALL (MEASURE_UPD_SWEEP | MEASURE_UPD_FREQ)

// Include measure functions
#ifdef __VNA_MEASURE_MODULE__
#define cell_printf cell_printf_bound
// legacy_measure.c expects invalidate_rect to be available for marking dirty regions.
// Provide a compatibility alias to the pixel-based helper defined above.
#define invalidate_rect invalidate_rect_px
#include "../../rf/analysis/legacy_measure.c"
#undef invalidate_rect
#undef cell_printf
#endif

static const struct {
  uint8_t option;
  uint8_t update;
  measure_cell_cb_t measure_cell;
  measure_prepare_cb_t measure_prepare;
} measure[] = {
    [MEASURE_NONE] = {MESAURE_NONE, 0, NULL, NULL},
#ifdef __USE_LC_MATCHING__
    [MEASURE_LC_MATH] = {MESAURE_NONE, MEASURE_UPD_ALL, draw_lc_match, prepare_lc_match},
#endif
#ifdef __S21_MEASURE__
    [MEASURE_SHUNT_LC] = {MESAURE_S21, MEASURE_UPD_SWEEP, draw_serial_result, prepare_series},
    [MEASURE_SERIES_LC] = {MESAURE_S21, MEASURE_UPD_SWEEP, draw_serial_result, prepare_series},
    [MEASURE_SERIES_XTAL] = {MESAURE_S21, MEASURE_UPD_SWEEP, draw_serial_result, prepare_series},
    [MEASURE_FILTER] = {MESAURE_S21, MEASURE_UPD_SWEEP, draw_filter_result, prepare_filter},
#endif
#ifdef __S11_CABLE_MEASURE__
    [MEASURE_S11_CABLE] = {MESAURE_S11, MEASURE_UPD_ALL, draw_s11_cable, prepare_s11_cable},
#endif
#ifdef __S11_RESONANCE_MEASURE__
    [MEASURE_S11_RESONANCE] = {MESAURE_S11, MEASURE_UPD_ALL, draw_s11_resonance,
                               prepare_s11_resonance},
#endif
};

static inline void measure_set_flag(uint8_t flag) {
  data_update |= flag;
}

void plot_set_measure_mode(uint8_t mode) {
  if (mode >= MEASURE_END)
    return;
  current_props._measure = mode;
  data_update = 0xFF;
  request_to_redraw(REDRAW_AREA);
}

uint16_t plot_get_measure_channels(void) {
  return measure[current_props._measure].option;
}

static void measure_prepare(void) {
  if (current_props._measure >= MEASURE_END)
    return;
  measure_prepare_cb_t measure_cb = measure[current_props._measure].measure_prepare;
  // Do measure and cache data only if update flags some
  if (measure_cb && (data_update & measure[current_props._measure].update))
    measure_cb(current_props._measure, data_update);
  data_update = 0;
}

static void cell_draw_measure(RenderCellCtx* rcx) {
  if (current_props._measure >= MEASURE_END)
    return;
  measure_cell_cb_t measure_draw_cb = measure[current_props._measure].measure_cell;
  if (measure_draw_cb) {
    lcd_set_colors(LCD_MEASURE_COLOR, LCD_BG_COLOR);
    measure_draw_cb(STR_MEASURE_X - rcx->x0, STR_MEASURE_Y - rcx->y0);
  }
}
#endif

//**************************************************************************************
//           Calculate and cache point coordinates for trace
//**************************************************************************************
// trace_into_index moved to traces.c        

//**************************************************************************************
//           Build graph data and cache it for output
//**************************************************************************************
static void plot_into_index(void) {
  const uint16_t points_now = sweep_points;
  const uint32_t trace_mask_now = gather_trace_mask(NULL);
  if (last_plotted_sweep_points != points_now || last_plotted_trace_mask != trace_mask_now) {
    request_to_redraw(REDRAW_AREA);
    last_plotted_sweep_points = points_now;
    last_plotted_trace_mask = trace_mask_now;
  }
  // Mark old markers for erase
  markmap_all_markers();
  //  START_PROFILE;
  // Cache trace data indexes, and mark plot area for update
  for (int t = 0; t < TRACES_MAX; t++)
    if (trace[t].enabled)
      trace_into_index(t);
  //  STOP_PROFILE;
  // Marker track on data update
  if (props_mode & TD_MARKER_TRACK)
    marker_search();
#ifdef __VNA_MEASURE_MODULE__
  // Current scan update
  measure_set_flag(MEASURE_UPD_SWEEP);
#endif
  // Mark for update cells, and add markers
  request_to_redraw(REDRAW_MARKER | REDRAW_CELLS);
}

//**************************************************************************************
//            Grid line values
//**************************************************************************************
#if VNA_ENABLE_GRID_VALUES
static void cell_draw_grid_values(RenderCellCtx* rcx) {
  // Skip not selected trace
  if (current_trace == TRACE_INVALID)
    return;
  // Skip for SMITH/POLAR and off trace
  uint32_t trace_type = 1 << trace[current_trace].type;
  if (trace_type & ROUND_GRID_MASK)
    return;
  cell_set_font(FONT_SMALL);
  // Render at right
  int16_t xpos = (int16_t)(GRID_X_TEXT - rcx->x0);
  int16_t ypos = (int16_t)(-rcx->y0 + 2);
  // Get top value
  float scale = get_trace_scale(current_trace);
  float ref = NGRIDY - get_trace_refpos(current_trace);
  if (trace_type & (1 << TRC_SWR))
    ref += 1.0f / scale; // For SWR trace, value shift by 1.0
  // Render grid values
  lcd_set_foreground(LCD_TRACE_1_COLOR + current_trace);
  do {
    cell_printf_ctx(rcx, xpos, ypos, "% 6.3F", ref * scale);
    ref -= 1.0f;
  } while ((ypos += GRIDY) < (int16_t)CELLHEIGHT);
  cell_set_font(FONT_NORMAL);
}

static void markmap_grid_values(void) {
  if (VNA_MODE(VNA_MODE_SHOW_GRID))
    invalidate_rect_px(GRID_X_TEXT, 0, LCD_WIDTH - OFFSETX, LCD_HEIGHT - 1);
}
#else
static void markmap_grid_values(void) {}
#endif

//**************************************************************************************
//                      All markers text render
//**************************************************************************************
// Marker and trace data position
static const struct {
  uint16_t x, y;
} marker_pos[MARKERS_MAX] = {
    {1 + CELLOFFSETX, 1},
    {1 + (WIDTH / 2) + CELLOFFSETX, 1},
    {1 + CELLOFFSETX, 1 + FONT_STR_HEIGHT},
    {1 + (WIDTH / 2) + CELLOFFSETX, 1 + FONT_STR_HEIGHT},
    {1 + CELLOFFSETX, 1 + 2 * FONT_STR_HEIGHT},
    {1 + (WIDTH / 2) + CELLOFFSETX, 1 + 2 * FONT_STR_HEIGHT},
    {1 + CELLOFFSETX, 1 + 3 * FONT_STR_HEIGHT},
    {1 + (WIDTH / 2) + CELLOFFSETX, 1 + 3 * FONT_STR_HEIGHT},
};

#ifdef LCD_320x240
#if _USE_FONT_ < 1
#define MARKER_FREQ "%.6q" S_Hz
#else
#define MARKER_FREQ "%.3q" S_Hz
#endif
#define MARKER_FREQ_SIZE 67
#endif
#ifdef LCD_480x320
#define MARKER_FREQ "%q" S_Hz
#define MARKER_FREQ_SIZE 116
#endif

static void cell_draw_marker_info(RenderCellCtx* rcx) {
  int t, mk, xpos, ypos;
  if (active_marker == MARKER_INVALID)
    return;
  int active_marker_idx = markers[active_marker].index;
  int j = 0;
  // Marker (for current selected trace) display mode (selected more then 1 marker, and minimum one
  // trace)
  if (previous_marker != MARKER_INVALID && current_trace != TRACE_INVALID) {
    t = current_trace;
    for (mk = 0; mk < MARKERS_MAX; mk++) {
      if (!markers[mk].enabled)
        continue;
      xpos = (int)marker_pos[j].x - rcx->x0;
      ypos = (int)marker_pos[j].y - rcx->y0;
      j++;
      lcd_set_foreground(LCD_TRACE_1_COLOR + t);
      if (mk == active_marker && lever_mode == LM_MARKER)
        cell_printf_ctx(rcx, xpos, ypos, S_SARROW);
      xpos += FONT_WIDTH;
      cell_printf_ctx(rcx, xpos, ypos, "M%d", mk + 1);
      xpos += 3 * FONT_WIDTH - 2;
      int32_t delta_index = -1;
      uint32_t mk_index = markers[mk].index;
      freq_t freq = get_marker_frequency(mk);
      if ((props_mode & TD_MARKER_DELTA) && mk != active_marker) {
        freq_t freq1 = get_marker_frequency(active_marker);
        freq_t delta = freq > freq1 ? freq - freq1 : freq1 - freq;
        delta_index = active_marker_idx;
        cell_printf_ctx(rcx, xpos, ypos, S_DELTA MARKER_FREQ, delta);
      } else {
        cell_printf_ctx(rcx, xpos, ypos, MARKER_FREQ, freq);
      }
      xpos += MARKER_FREQ_SIZE;
      lcd_set_foreground(LCD_FG_COLOR);
      trace_print_value_string(rcx, xpos, ypos, t, mk_index, delta_index);
    }
    // Marker frequency data print
    xpos = 21 + (WIDTH / 2) + CELLOFFSETX - rcx->x0;
    ypos = 1 + ((j + 1) / 2) * FONT_STR_HEIGHT - rcx->y0;
    // draw marker delta
    if (!(props_mode & TD_MARKER_DELTA) && active_marker != previous_marker) {
      int previous_marker_idx = markers[previous_marker].index;
      cell_printf_ctx(rcx, xpos, ypos, S_DELTA "%d-%d:", active_marker + 1, previous_marker + 1);
      xpos += 5 * FONT_WIDTH + 2;
      if ((props_mode & DOMAIN_MODE) == DOMAIN_FREQ) {
        freq_t freq = get_marker_frequency(active_marker);
        freq_t freq1 = get_marker_frequency(previous_marker);
        freq_t delta = freq >= freq1 ? freq - freq1 : freq1 - freq;
        cell_printf_ctx(rcx, xpos, ypos, "%c%q" S_Hz, freq >= freq1 ? '+' : '-', delta);
      } else {
        cell_printf_ctx(rcx, xpos, ypos, "%F" S_SECOND " (%F" S_METRE ")",
                    time_of_index(active_marker_idx) - time_of_index(previous_marker_idx),
                    distance_of_index(active_marker_idx) - distance_of_index(previous_marker_idx));
      }
    }
  } else /*if (active_marker != MARKER_INVALID)*/ { // Trace display mode
    for (t = 0; t < TRACES_MAX; t++) {
      if (!trace[t].enabled)
        continue;
      xpos = (int)marker_pos[j].x - rcx->x0;
      ypos = (int)marker_pos[j].y - rcx->y0;
      j++;
      lcd_set_foreground(LCD_TRACE_1_COLOR + t);
      if (t == current_trace)
        cell_printf_ctx(rcx, xpos, ypos, S_SARROW);
      xpos += FONT_WIDTH;
      cell_printf_ctx(rcx, xpos, ypos, get_trace_chname(t));
      xpos += 4 * FONT_WIDTH - 2;

      int n = trace_print_info(rcx, xpos, ypos, t) + 1;
      xpos += n * FONT_WIDTH - 5;
      lcd_set_foreground(LCD_FG_COLOR);
      trace_print_value_string(rcx, xpos, ypos, t, active_marker_idx, -1);
    }
    // Marker frequency data print
    xpos = 21 + (WIDTH / 2) + CELLOFFSETX - rcx->x0;
    ypos = 1 + ((j + 1) / 2) * FONT_STR_HEIGHT - rcx->y0;
    // draw marker frequency
    if (lever_mode == LM_MARKER)
      cell_printf_ctx(rcx, xpos, ypos, S_SARROW);
    xpos += FONT_WIDTH;
    cell_printf_ctx(rcx, xpos, ypos, "M%d:", active_marker + 1);
    xpos += 3 * FONT_WIDTH + 4;
    if ((props_mode & DOMAIN_MODE) == DOMAIN_FREQ)
      cell_printf_ctx(rcx, xpos, ypos, "%q" S_Hz, get_marker_frequency(active_marker));
    else
      cell_printf_ctx(rcx, xpos, ypos, "%F" S_SECOND " (%F" S_METRE ")", time_of_index(active_marker_idx),
                  distance_of_index(active_marker_idx));
  }

  xpos = 1 + 18 + CELLOFFSETX - rcx->x0;
  ypos = 1 + ((j + 1) / 2) * FONT_STR_HEIGHT - rcx->y0;
  float electrical_delay = get_electrical_delay();
  if (electrical_delay != 0.0f) { // draw electrical delay
    char sel = lever_mode == LM_EDELAY ? S_SARROW[0] : ' ';
    cell_printf_ctx(rcx, xpos, ypos, "%cEdelay: %F" S_SECOND " (%F" S_METRE ")", sel, electrical_delay,
                electrical_delay * (SPEED_OF_LIGHT / 100.0f) * velocity_factor);
    ypos += FONT_STR_HEIGHT;
  }
  if (s21_offset != 0.0f) { // draw s21 offset
    cell_printf_ctx(rcx, xpos, ypos, "S21 offset: %.3F" S_dB, s21_offset);
    ypos += FONT_STR_HEIGHT;
  }
#ifdef __VNA_Z_RENORMALIZATION__
  if (current_props._portz != 50.0f) {
    cell_printf_ctx(rcx, xpos, ypos, "PORT-Z: 50 " S_RARROW " %F" S_OHM, current_props._portz);
    ypos += FONT_STR_HEIGHT;
  }
#endif
}


void render_overlays(RenderCellCtx* rcx) {
  cell_draw_all_refpos(rcx);
#ifdef __VNA_MEASURE_MODULE__
  cell_draw_measure(rcx);
#endif
#if VNA_ENABLE_GRID_VALUES
  if (VNA_MODE(VNA_MODE_SHOW_GRID) && rcx->x0 > (GRID_X_TEXT - CELLWIDTH))
    cell_draw_grid_values(rcx);
#endif
  if (rcx->y0 <= marker_area_max())
    cell_draw_marker_info(rcx);
}

// Un-static for build fix
void draw_cell(int x0, int y0) {
  int w = CELLWIDTH;
  int h = CELLHEIGHT;
  if (w > area_width - x0)
    w = area_width - x0;
  if (h > area_height - y0)
    h = area_height - y0;
  if (w <= 0 || h <= 0)
    return;
  RenderCellCtx rcx = render_cell_ctx(x0, y0, (uint16_t)w, (uint16_t)h, lcd_get_cell_buffer());
  set_active_cell_ctx(&rcx);
  cell_clear(&rcx, GET_PALTETTE_COLOR(LCD_BG_COLOR));
  bool smith_impedance = false;
  uint32_t trace_mask = gather_trace_mask(&smith_impedance);
  pixel_t grid_color = GET_PALTETTE_COLOR(LCD_GRID_COLOR);
  if (trace_mask & RECTANGULAR_GRID_MASK)
    render_rectangular_grid_layer(&rcx, grid_color);
  if (trace_mask & (ROUND_GRID_MASK))
    render_round_grid_layer(&rcx, grid_color, trace_mask, smith_impedance);
  render_traces_in_cell(&rcx);
  render_markers_in_cell(&rcx);
  render_overlays(&rcx);
  compact_cell_buffer(&rcx);
  lcd_bulk_continue(OFFSETX + x0, OFFSETY + y0, rcx.w, rcx.h);
  set_active_cell_ctx(NULL);
}

void set_area_size(uint16_t w, uint16_t h) {
  area_width = w;
  area_height = h;
}

void draw_all_cells(void) {
  uint16_t m, n;
  uint16_t w = (area_width + CELLWIDTH - 1) / CELLWIDTH;
  uint16_t h = (area_height + CELLHEIGHT - 1) / CELLHEIGHT;
#ifdef __VNA_MEASURE_MODULE__
  measure_prepare();
#endif
  for (n = 0; n < h; n++) {
    map_t update_map = markmap[n];
    for (m = 0; update_map && m < w; update_map >>= 1, m++)
      if (update_map & 1)
        draw_cell(m * CELLWIDTH, n * CELLHEIGHT);
  }

#if 0
  lcd_bulk_finish();
  for (m = 0; m < w; m++)
    for (n = 0; n < h; n++) {
      lcd_set_background((markmap[n] & (1 << m)) ? LCD_LOW_BAT_COLOR : LCD_NORMAL_BAT_COLOR);
      lcd_fill(m*CELLWIDTH+OFFSETX, n*CELLHEIGHT, 2, 2);
    }
#endif
  // clear map for next plotting
  clear_markmap();
  // Flush LCD buffer, wait completion (need call after end use lcd_bulk_continue mode)
  lcd_bulk_finish();
  //  STOP_PROFILE
}

//
// Call this function then need fast draw marker and marker info
// Used in ui.c for leveler move marker, drag marker and etc.
void redraw_marker(int8_t marker) {
  if (marker == MARKER_INVALID || !markers[marker].enabled)
    return;
#ifdef __VNA_MEASURE_MODULE__
  if (marker == active_marker)
    measure_set_flag(MEASURE_UPD_FREQ);
#endif
  // Mark for update marker and text
  request_to_draw_marker(markers[marker].index);
  markmap_marker_area();
  redraw_request &= ~(REDRAW_MARKER); // reset all marker update
  redraw_request |= REDRAW_CELLS;     // Update cells
  draw_all();
}

static void draw_frequencies(void) {
  char lm0 = lever_mode == LM_FREQ_0 ? S_SARROW[0] : ' ';
  char lm1 = lever_mode == LM_FREQ_1 ? S_SARROW[0] : ' ';
  // Draw frequency string
  lcd_set_colors(LCD_FG_COLOR, LCD_BG_COLOR);
  lcd_fill(0, HEIGHT + OFFSETY + 1, LCD_WIDTH, LCD_HEIGHT - HEIGHT - OFFSETY - 1);
  lcd_set_font(FONT_SMALL);
  // Prepare text for frequency string
  if ((props_mode & DOMAIN_MODE) == DOMAIN_FREQ) {
    if (FREQ_IS_CW()) {
      lcd_printf(FREQUENCIES_XPOS1, FREQUENCIES_YPOS, "%c%s %15q" S_Hz, lm0, "CW",
                 get_sweep_frequency(ST_CW));
    } else if (FREQ_IS_STARTSTOP()) {
      lcd_printf(FREQUENCIES_XPOS1, FREQUENCIES_YPOS, "%c%s %15q" S_Hz, lm0, "START",
                 get_sweep_frequency(ST_START));
      lcd_printf(FREQUENCIES_XPOS2, FREQUENCIES_YPOS, "%c%s %15q" S_Hz, lm1, "STOP",
                 get_sweep_frequency(ST_STOP));
    } else if (FREQ_IS_CENTERSPAN()) {
      lcd_printf(FREQUENCIES_XPOS1, FREQUENCIES_YPOS, "%c%s %15q" S_Hz, lm0, "CENTER",
                 get_sweep_frequency(ST_CENTER));
      lcd_printf(FREQUENCIES_XPOS2, FREQUENCIES_YPOS, "%c%s %15q" S_Hz, lm1, "SPAN",
                 get_sweep_frequency(ST_SPAN));
    }
  } else {
    lcd_printf(FREQUENCIES_XPOS1, FREQUENCIES_YPOS, "START 0" S_SECOND "    VF = %d%%",
               velocity_factor);
    lcd_printf(FREQUENCIES_XPOS2, FREQUENCIES_YPOS, "STOP %F" S_SECOND " (%F" S_METRE ")",
               time_of_index(sweep_points - 1), distance_of_index(sweep_points - 1));
  }
  // Draw bandwidth and point count
  lcd_set_foreground(LCD_BW_TEXT_COLOR);
  lcd_printf(FREQUENCIES_XPOS3, FREQUENCIES_YPOS, "BW:%u" S_Hz " %up",
             get_bandwidth_frequency(config._bandwidth), sweep_points);
  lcd_set_font(FONT_NORMAL);
}

//**************************************************************************************
//            Draw/update calibration status panel
//**************************************************************************************
static void draw_cal_status(void) {
  uint32_t i;
  int x = CALIBRATION_INFO_POSX;
  int y = CALIBRATION_INFO_POSY;
  lcd_set_colors(LCD_DISABLE_CAL_COLOR, LCD_BG_COLOR);
  lcd_fill(x, y, OFFSETX - x, 10 * (sFONT_STR_HEIGHT));
  lcd_set_font(FONT_SMALL);
  if (cal_status & CALSTAT_APPLY) {
    // Set 'C' string for slot status
    char c[4] = {'C', '0' + lastsaveid, 0, 0};
    if (lastsaveid == NO_SAVE_SLOT)
      c[1] = '*';
    if (cal_status & CALSTAT_INTERPOLATED) {
      lcd_set_foreground(LCD_INTERP_CAL_COLOR);
      c[0] = 'c';
    } else
      lcd_set_foreground(LCD_FG_COLOR);
    lcd_drawstring(x, y, c);
    lcd_set_foreground(LCD_FG_COLOR);
  }

  static const struct {
    char text, zero;
    uint16_t mask;
  } calibration_text[] = {
      {'O', 0, CALSTAT_OPEN}, {'S', 0, CALSTAT_SHORT}, {'D', 0, CALSTAT_ED},
      {'R', 0, CALSTAT_ER},   {'S', 0, CALSTAT_ES},    {'T', 0, CALSTAT_ET},
      {'t', 0, CALSTAT_THRU}, {'X', 0, CALSTAT_EX},    {'E', 0, CALSTAT_ENHANCED_RESPONSE}};
  for (i = 0; i < ARRAY_COUNT(calibration_text); i++)
    if (cal_status & calibration_text[i].mask)
      lcd_drawstring(x, y += sFONT_STR_HEIGHT, &calibration_text[i].text);

  if ((cal_status & CALSTAT_APPLY) && cal_power != current_props._power)
    lcd_set_foreground(LCD_DISABLE_CAL_COLOR);

  // 2,4,6,8 mA power or auto
  lcd_printf(x, y += sFONT_STR_HEIGHT, "P%c",
             current_props._power > 3 ? ('a') : (current_props._power * 2 + '2'));
#ifdef __USE_SMOOTH__
  y += FONT_STR_HEIGHT;
  uint8_t smooth = get_smooth_factor();
  if (smooth > 0) {
    lcd_set_foreground(LCD_FG_COLOR);
    lcd_printf(x, y += sFONT_STR_HEIGHT, "s%d", smooth);
  }
#endif
  lcd_set_font(FONT_NORMAL);
}

//**************************************************************************************
//            Draw battery level
//**************************************************************************************
#define BATTERY_TOP_LEVEL 4100
#define BATTERY_BOTTOM_LEVEL 3200
#define BATTERY_WARNING_LEVEL 3300
static void draw_battery_status(void) {
  int16_t vbat = adc_vbat_read();
  static int16_t last_drawn_vbat;
  static bool last_vbat_initialized = false;
  if (!last_vbat_initialized) {
    last_drawn_vbat = INT16_MIN;
    last_vbat_initialized = true;
  }
  if (vbat <= 0 && last_drawn_vbat <= 0)
    return;
  if (vbat == last_drawn_vbat)
    return;
  last_drawn_vbat = vbat;
  uint8_t string_buf[24];
  // Set battery color
  lcd_set_colors(vbat < BATTERY_WARNING_LEVEL ? LCD_LOW_BAT_COLOR : LCD_NORMAL_BAT_COLOR,
                 LCD_BG_COLOR);
  //  plot_printf(string_buf, sizeof string_buf, "V:%d", vbat);
  //  lcd_drawstringV(string_buf, 1, 60);
  // Prepare battery bitmap image
  // Battery top
  int x = 0;
  string_buf[x++] = 0b00000000;
  string_buf[x++] = 0b00111100;
  string_buf[x++] = 0b00111100;
  string_buf[x++] = 0b11111111;
  // Fill battery status
  for (int power = BATTERY_TOP_LEVEL; power > BATTERY_BOTTOM_LEVEL;) {
    if ((x & 3) == 0) {
      string_buf[x++] = 0b10000001;
      continue;
    }
    string_buf[x++] = (power > vbat) ? 0b10000001 : // Empty line
                          0b10111101;               // Full line
    power -= 100;
  }
  // Battery bottom
  string_buf[x++] = 0b10000001;
  string_buf[x++] = 0b11111111;
  // Draw battery
  lcd_blit_bitmap(BATTERY_ICON_POSX, BATTERY_ICON_POSY, 8, x, string_buf);
}

//**************************************************************************************
//            Draw all request
//**************************************************************************************
void draw_all(void) {
#ifdef __USE_BACKUP__
  if (redraw_request & REDRAW_BACKUP)
    update_backup_data();
#endif
  if (redraw_request & REDRAW_PLOT)
    plot_into_index();
  if (area_width == 0) {
    redraw_request = 0;
    return;
  }
  if (redraw_request & REDRAW_CLRSCR) {
    lcd_set_background(LCD_BG_COLOR);
    lcd_clear_screen();
  }
  if (redraw_request & REDRAW_AREA)
    force_set_markmap();
  else {
    if (redraw_request & REDRAW_MARKER)
      markmap_all_markers();
    if (redraw_request & REDRAW_REFERENCE)
      markmap_all_refpos();
#if VNA_ENABLE_GRID_VALUES
    if (redraw_request & REDRAW_GRID_VALUE)
      markmap_grid_values();
#endif
  }
  if (redraw_request &
      (REDRAW_CELLS | REDRAW_MARKER | REDRAW_GRID_VALUE | REDRAW_REFERENCE | REDRAW_AREA))
    draw_all_cells();
  if (redraw_request & REDRAW_FREQUENCY)
    draw_frequencies();
  if (redraw_request & REDRAW_CAL_STATUS)
    draw_cal_status();
  if (redraw_request & REDRAW_BATTERY)
    draw_battery_status();
  redraw_request = 0;
}

//**************************************************************************************
//            Set update mask for next screen update
//**************************************************************************************
void request_to_redraw(uint16_t mask) {
  redraw_request |= mask;
}

void plot_init(void) {
#if _USE_FONT_ != _USE_SMALL_FONT_
  cell_set_font(FONT_NORMAL);
#endif
  area_width = AREA_WIDTH_NORMAL;
  area_height = AREA_HEIGHT_NORMAL;
  request_to_redraw(REDRAW_PLOT | REDRAW_ALL);
  draw_all();
}
