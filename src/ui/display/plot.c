/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
 * Based on TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
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
#include "../resources/icons/icons_marker.c"
#include "ui/display/plot_internal.h"


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

// Cell render use spi buffer
// RenderCellCtx moved to plot_internal.h

// MarkLineState moved to plot_trace.c

/**
 * @brief Result bounds for locating sweep indices within a cell.
 */
typedef struct {
  bool found;
  uint16_t i0;
  uint16_t i1;
} TraceIndexRange;

_Static_assert(CELLWIDTH % 8 == 0, "CELLWIDTH must be a multiple of 8");
_Static_assert(SWEEP_POINTS_MAX > 0, "Sweep points must be positive");

static inline uint16_t clamp_u16(int value, uint16_t min_value, uint16_t max_value) {
  if (value < (int)min_value)
    return min_value;
  if (value > (int)max_value)
    return max_value;
  return (uint16_t)value;
}

// cell_ptr moved to plot_internal.h

/**
 * @brief Clear the cell buffer to a solid color using word-sized stores.
 */




// indicate dirty cells (not redraw if cell data not changed)
#define MAX_MARKMAP_X ((LCD_WIDTH + CELLWIDTH - 1) / CELLWIDTH)
#define MAX_MARKMAP_Y ((LCD_HEIGHT + CELLHEIGHT - 1) / CELLHEIGHT)
// Define markmap mask size
#if MAX_MARKMAP_X <= 8
typedef uint8_t map_t;
#elif MAX_MARKMAP_X <= 16
typedef uint16_t map_t;
#elif MAX_MARKMAP_X <= 32
typedef uint32_t map_t;
#endif
_Static_assert(MAX_MARKMAP_X <= 32, "markmap type must handle at most 32 columns");

static inline map_t markmap_mask(uint16_t x_begin, uint16_t x_end);

static map_t markmap[MAX_MARKMAP_Y];

/**
 * @brief Create a horizontal bitmask covering the inclusive column range.
 */
static inline map_t markmap_mask(uint16_t x_begin, uint16_t x_end) {
  if (x_begin > x_end)
    SWAP(uint16_t, x_begin, x_end);
  const uint16_t bitcount = (uint16_t)(sizeof(map_t) * CHAR_BIT);
  if (x_begin >= bitcount)
    return 0;
  uint16_t width = (uint16_t)(x_end - x_begin + 1);
  if (width >= bitcount)
    return (map_t)~(map_t)0;
  if ((uint32_t)x_begin + width > bitcount)
    // Clamp when the requested range would exceed the markmap representation.
    width = (uint16_t)(bitcount - x_begin);
  const map_t width_mask = (map_t)(((map_t)1u << width) - 1u);
  return (map_t)(width_mask << x_begin);
}
// Trace struct and tables moved to plot_trace.c

// Grid rendering moved to plot_grid.c

/**
 * @brief Compact cell buffer for clipped cells at the right edge.
 *
 * Copies only the visible columns to keep LCD DMA transfers contiguous.
 */
static void compact_cell_buffer(RenderCellCtx* rcx) {
  if (rcx->width >= CELLWIDTH)
    return;
  chDbgAssert(rcx->width <= CELLWIDTH, "invalid cell width");
  pixel_t* src = rcx->buf + CELLWIDTH;
  pixel_t* dst = rcx->buf + rcx->width;
  for (uint16_t row = 1; row < rcx->height; ++row) {
    for (uint16_t col = 0; col < rcx->width; ++col)
      *dst++ = *src++;
    src += CELLWIDTH - rcx->width;
  }
}

// Trace rendering and marker logic moved to plot_trace.c and plot_marker.c

/**
 * @brief Draw overlay information such as marker text, measurements, and references.
 */
// render_overlays moved to plot_marker.c

//**************************************************************************************
// Cell render functions
//**************************************************************************************
#if VNA_FAST_RENDER
// Little faster on easy traces, 2x faster if need lot of clipping and draw long lines
// Bitmaps draw, 2x faster, but limit width <= 32
#include "fast_render/vna_render.c"
#else
// Little slower on easy traces, but slow if need lot of clip and draw long lines
/**
 * @brief Draw a line in the cell buffer using optimized Bresenham's algorithm.
 * 
 * This function draws a line between two points in the cell buffer, with optimized
 * boundary checking and clipping to avoid expensive operations outside the cell.
 */
void cell_drawline(const RenderCellCtx* rcx, int x0, int y0, int x1, int y1, pixel_t c) {
  // Quick out-of-bounds check to avoid expensive computation
  if ((x0 < 0 && x1 < 0) || (y0 < 0 && y1 < 0) || 
      (x0 >= CELLWIDTH && x1 >= CELLWIDTH) || (y0 >= CELLHEIGHT && y1 >= CELLHEIGHT))
    return;

  // Optimized Bresenham's algorithm implementation
  int dx = x1 - x0;
  int dy = y1 - y0;
  
  // Determine direction
  int sx = (dx > 0) ? 1 : -1;
  int sy = (dy > 0) ? 1 : -1;
  
  // Work with absolute values
  dx = (dx < 0) ? -dx : dx;
  dy = (dy < 0) ? -dy : dy;
  
  int err = dx - dy;
  int x = x0;
  int y = y0;
  
  // Keep looping while we're within bounds
  while (1) {
    // Only draw pixel if within cell bounds
    if ((uint32_t)x < rcx->width && (uint32_t)y < rcx->height)
      *cell_ptr(rcx, (uint16_t)x, (uint16_t)y) = c;
    
    // Check if we've reached the end point
    if (x == x1 && y == y1)
      break;
      
    int e2 = 2 * err;
    
    if (e2 > -dy) {
      err -= dy;
      x += sx;
    }
    if (e2 < dx) {
      err += dx;
      y += sy;
    }
  }
}

// Slower, but allow any width bitmaps
void cell_blit_bitmap(RenderCellCtx* rcx, int16_t x, int16_t y, uint16_t w, uint16_t h,
                             const uint8_t* bmp) {
  int16_t x1, y1;
  if ((x1 = x + w) < 0 || (y1 = y + h) < 0)
    return;
  if (y1 >= (int16_t)rcx->height)
    y1 = (int16_t)rcx->height; // clip bottom
  if (y < 0) {
    bmp -= y * ((w + 7) >> 3);
    y = 0;
  } // clip top
  for (uint8_t bits = 0; y < y1; y++) {
    for (int r = 0; r < w; r++, bits <<= 1) {
      if ((r & 7) == 0)
        bits = *bmp++;
      if ((0x80 & bits) == 0)
        continue; // no pixel
      if ((uint32_t)(x + r) >= rcx->width)
        continue; // x+r < 0 || x+r >= CELLWIDTH
      *cell_ptr(rcx, (uint16_t)(x + r), (uint16_t)y) = foreground_color;
    }
  }
}
#endif

#ifdef VNA_ENABLE_SHADOW_TEXT
void cell_blit_bitmap_shadow(RenderCellCtx* rcx, int16_t x, int16_t y, uint16_t w, uint16_t h,
                                    const uint8_t* bmp) {
  int i;
  if (x + w < 0 || h + y < 0)
    return; // Clipping
  // Prepare shadow bitmap
  uint16_t dst[16], mask = 0xFFFF << (16 - w), p;
  dst[0] = dst[1] = 0;
  if (h > ARRAY_COUNT(dst) - 2)
    h = ARRAY_COUNT(dst) - 2;
  for (i = 0; i < h; i++) {
    p = (bmp[i] << 8) & mask; // extend from 8 bit width to 16 bit
    p |= (p >> 1) | (p >> 2); // shadow horizontally
    p = (p >> 8) | (p << 8);  // swap bytes (render do by 8 bit)
    dst[i + 2] = p;           // shadow vertically
    dst[i + 1] |= dst[i + 2];
    dst[i] |= dst[i + 1];
  }
  // Render shadow on cell
  pixel_t t = foreground_color;             // remember color
  lcd_set_foreground(LCD_TXT_SHADOW_COLOR); // set shadow color
  w += 2;
  h += 2; // Shadow size > by 2 pixel
  cell_blit_bitmap(rcx, x - 1, y - 1, w < 9 ? 9 : w, h, (uint8_t*)dst);
  foreground_color = t; // restore color
}
#endif

//**************************************************************************************
// Cell printf function
//**************************************************************************************
typedef struct {
  const void* vmt;
  RenderCellCtx* ctx;
  int16_t x;
  int16_t y;
} cellPrintStream;

static void put_normal(cellPrintStream* ps, uint8_t ch) {
  uint16_t w = FONT_GET_WIDTH(ch);
#if VNA_ENABLE_SHADOW_TEXT
  cell_blit_bitmap_shadow(ps->ctx, ps->x, ps->y, w, FONT_GET_HEIGHT, FONT_GET_DATA(ch));
#endif
#if _USE_FONT_ < 3
  cell_blit_bitmap(ps->ctx, ps->x, ps->y, w, FONT_GET_HEIGHT, FONT_GET_DATA(ch));
#else
  cell_blit_bitmap(ps->ctx, ps->x, ps->y, w < 9 ? 9 : w, FONT_GET_HEIGHT, FONT_GET_DATA(ch));
#endif
  ps->x += w;
}

#if _USE_FONT_ != _USE_SMALL_FONT_
typedef void (*font_put_t)(cellPrintStream* ps, uint8_t ch);
static font_put_t put_char;
static void put_small(cellPrintStream* ps, uint8_t ch) {
  uint16_t w = sFONT_GET_WIDTH(ch);
#if VNA_ENABLE_SHADOW_TEXT
  cell_blit_bitmap_shadow(ps->ctx, ps->x, ps->y, w, sFONT_GET_HEIGHT, sFONT_GET_DATA(ch));
#endif
#if _USE_SMALL_FONT_ < 3
  cell_blit_bitmap(ps->ctx, ps->x, ps->y, w, sFONT_GET_HEIGHT, sFONT_GET_DATA(ch));
#else
  cell_blit_bitmap(ps->ctx, ps->x, ps->y, w < 9 ? 9 : w, sFONT_GET_HEIGHT, sFONT_GET_DATA(ch));
#endif
  ps->x += w;
}
static inline void cell_set_font(int type) {
  put_char = type == FONT_SMALL ? put_small : put_normal;
}

#else
#define cell_set_font(type)                                                                        \
  {                                                                                                \
  }
#define put_char put_normal
#endif

static msg_t cell_put(void* ip, uint8_t ch) {
  cellPrintStream* ps = ip;
  if (ps->x < CELLWIDTH && ps->y < CELLHEIGHT)
    put_char(ps, ch);
  return MSG_OK;
}

// Simple print in buffer function
int cell_vprintf(RenderCellCtx* rcx, int16_t x, int16_t y, const char* fmt, va_list ap) {
  static const struct lcd_printStreamVMT {
    _base_sequential_stream_methods
  } cell_vmt = {NULL, NULL, cell_put, NULL};
  // Skip print if not on cell (at top/bottom/right)
  if ((uint32_t)(y + FONT_GET_HEIGHT) >= CELLHEIGHT + FONT_GET_HEIGHT || x >= CELLWIDTH)
    return 0;
  // Init small cell print stream
  cellPrintStream ps = {&cell_vmt, rcx, x, y};
  // Performing the print operation using the common code.
  int retval = chvprintf((BaseSequentialStream*)(void*)&ps, fmt, ap);
  // Return number of bytes that would have been written.
  return retval;
}

int cell_printf_ctx(RenderCellCtx* rcx, int16_t x, int16_t y, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int retval = cell_vprintf(rcx, x, y, fmt, ap);
  va_end(ap);
  return retval;
}

// Bound during draw_cell() so measurement helpers can reuse printf utilities.
RenderCellCtx* active_cell_ctx = NULL;

// cell_printf_bound moved to plot_marker.c

//**************************************************************************************
// Cell mark map functions
//**************************************************************************************
void plot_mark_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  uint16_t cell_x1 = (uint16_t)(x1 / CELLWIDTH);
  uint16_t cell_x2 = (uint16_t)(x2 / CELLWIDTH);
  uint16_t cell_y1 = (uint16_t)(y1 / CELLHEIGHT);
  uint16_t cell_y2 = (uint16_t)(y2 / CELLHEIGHT);
  if (cell_y1 >= MAX_MARKMAP_Y && cell_y2 >= MAX_MARKMAP_Y)
    return;
  if (cell_x1 >= MAX_MARKMAP_X && cell_x2 >= MAX_MARKMAP_X)
    return;
  if (cell_x1 >= MAX_MARKMAP_X)
    cell_x1 = MAX_MARKMAP_X - 1;
  if (cell_x2 >= MAX_MARKMAP_X)
    cell_x2 = MAX_MARKMAP_X - 1;
  if (cell_y1 >= MAX_MARKMAP_Y)
    cell_y1 = MAX_MARKMAP_Y - 1;
  if (cell_y2 >= MAX_MARKMAP_Y)
    cell_y2 = MAX_MARKMAP_Y - 1;
  map_t mask = markmap_mask(cell_x1, cell_x2);
  if (cell_y1 > cell_y2)
    SWAP(uint16_t, cell_y1, cell_y2);
  for (uint16_t row = cell_y1; row <= cell_y2 && row < MAX_MARKMAP_Y; ++row)
    markmap[row] |= mask;
}

// mark_set_index moved to plot_trace.c

static inline void clear_markmap(void) {
  int n = MAX_MARKMAP_Y - 1;
  do {
    markmap[n] = (map_t)0;
  } while (n--);
}

/*
 * Force full screen update
 */
void force_set_markmap(void) {
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
void plot_invalidate_rect(int x0, int y0, int x1, int y1) {
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
// PORT_Z definition moved to plot_internal.h
// Help functions
// Math, lists, and helpers moved to plot_trace.c / plot_marker.c

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
// search_index_range_x fully removed
// search_index_range_x removed

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
    plot_invalidate_rect(x, y, x + MARKER_WIDTH - 1, y + MARKER_HEIGHT - 1);
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
  plot_invalidate_rect(0, 0, AREA_WIDTH_NORMAL, marker_area_max());
}

void markmap_all_markers(void) {
  int i;
  for (i = 0; i < MARKERS_MAX; i++) {
    if (!markers[i].enabled)
      continue;
    request_to_draw_marker(markers[i].index);
  }
  markmap_marker_area();
}

// marker_search, search_nearest_index moved to plot_trace.c

//**************************************************************************************
//            Reference plate draw and update
//**************************************************************************************
static void markmap_all_refpos(void) {
  // Hardcoded, reference marker plates
  plot_invalidate_rect(0, 0, CELLOFFSETX + 1, AREA_HEIGHT_NORMAL);
}

// cell_draw_all_refpos moved to plot_marker.c

//**************************************************************************************
//            Update cells behind menu
//**************************************************************************************
void request_to_draw_cells_behind_menu(void) {
  // Values Hardcoded from ui.c
  plot_invalidate_rect(LCD_WIDTH - MENU_BUTTON_WIDTH - OFFSETX, 0, LCD_WIDTH - OFFSETX,
                    LCD_HEIGHT - 1);
  request_to_redraw(REDRAW_CELLS | REDRAW_FREQUENCY);
}

// Measure module code moved to plot_marker.c

//**************************************************************************************
//           Calculate and cache point coordinates for trace
//**************************************************************************************
// trace_into_index moved to plot_trace.c

//**************************************************************************************
//           Build graph data and cache it for output
//**************************************************************************************
static void plot_into_index(void) {
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
// cell_draw_grid_values moved to plot_grid.c

#if VNA_ENABLE_GRID_VALUES
static void markmap_grid_values(void) {
  if (VNA_MODE(VNA_MODE_SHOW_GRID))
    plot_invalidate_rect(GRID_X_TEXT, 0, LCD_WIDTH - OFFSETX, LCD_HEIGHT - 1);
}
#else
static void markmap_grid_values(void) {}
#endif

// Orphan code removed

static void draw_cell(int x0, int y0) {
  int w = CELLWIDTH;
  int h = CELLHEIGHT;
  if (w > area_width - x0)
    w = area_width - x0;
  if (h > area_height - y0)
    h = area_height - y0;
  if (w <= 0 || h <= 0)
    return;
  RenderCellCtx rcx = render_cell_ctx(x0, y0, (uint16_t)w, (uint16_t)h, lcd_get_cell_buffer());
  active_cell_ctx = &rcx;
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
  lcd_bulk_continue(OFFSETX + x0, OFFSETY + y0, rcx.width, rcx.height);
  active_cell_ctx = NULL;
}

void set_area_size(uint16_t w, uint16_t h) {
  area_width = w;
  area_height = h;
}

static void draw_all_cells(void) {
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
void request_to_redraw(uint32_t mask) {
  redraw_request |= mask;
}

void plot_init(void) {
#if _USE_FONT_ != _USE_SMALL_FONT_
  put_char = put_normal;
#endif
  area_width = AREA_WIDTH_NORMAL;
  area_height = AREA_HEIGHT_NORMAL;
  request_to_redraw(REDRAW_PLOT | REDRAW_ALL);
  draw_all();
}
