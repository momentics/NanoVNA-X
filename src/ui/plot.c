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
 *                        src/modules/vna_render.c (defaults to the legacy
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

static uint16_t area_width = AREA_WIDTH_NORMAL;
static uint16_t area_height = AREA_HEIGHT_NORMAL;

// Cell render use spi buffer
/**
 * @brief Rendering context for a single LCD cell.
 *
 * Coordinates are expressed in absolute screen pixels with (0,0) at the
 * top-left corner of the plot area.
 */
typedef struct {
  pixel_t* buf;
  uint16_t w;
  uint16_t h;
  uint16_t x0;
  uint16_t y0;
} RenderCellCtx;

/**
 * @brief Tracks state transitions when recomputing trace sample positions.
 */
typedef struct {
  uint16_t diff;
  uint16_t last_x;
  uint16_t last_y;
} MarkLineState;

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
_Static_assert(TRACE_INDEX_COUNT > 0, "Trace index count must be positive");

static inline uint16_t clamp_u16(int value, uint16_t min_value, uint16_t max_value) {
  if (value < (int)min_value)
    return min_value;
  if (value > (int)max_value)
    return max_value;
  return (uint16_t)value;
}

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

static inline pixel_t* cell_ptr(const RenderCellCtx* rcx, uint16_t x, uint16_t y) {
  return rcx->buf + (uint32_t)y * CELLWIDTH + x;
}

/**
 * @brief Clear the cell buffer to a solid color using word-sized stores.
 */
static inline void cell_clear(RenderCellCtx* rcx, pixel_t color) {
  chDbgAssert(((uintptr_t)rcx->buf % sizeof(uint32_t)) == 0,
              "cell buffer must be 32-bit aligned");
  const size_t rows = rcx->h;
  uint32_t* dst32 = (uint32_t*)rcx->buf;
#if LCD_PIXEL_SIZE == 2
  const uint32_t packed = (uint32_t)color | ((uint32_t)color << 16);
  const size_t stride32 = CELLWIDTH / 2;
  for (size_t y = 0; y < rows; ++y) {
    for (size_t x = 0; x < stride32; ++x)
      dst32[x] = packed;
    dst32 += stride32;
  }
#elif LCD_PIXEL_SIZE == 1
  const uint32_t packed = (uint32_t)color | ((uint32_t)color << 8) | ((uint32_t)color << 16) |
                          ((uint32_t)color << 24);
  const size_t stride32 = CELLWIDTH / 4;
  for (size_t y = 0; y < rows; ++y) {
    for (size_t x = 0; x < stride32; ++x)
      dst32[x] = packed;
    dst32 += stride32;
  }
#else
#error "Unsupported LCD pixel size"
#endif
}

/**
 * @brief Create a render context for the requested cell coordinates.
 */
static inline RenderCellCtx render_cell_ctx(int x0, int y0, uint16_t w, uint16_t h, pixel_t* buf) {
  RenderCellCtx ctx = {
      .buf = buf,
      .w = w,
      .h = h,
      .x0 = (uint16_t)x0,
      .y0 = (uint16_t)y0,
  };
  return ctx;
}

static inline uint32_t abs_u32(int32_t value) {
  return (uint32_t)(value < 0 ? -value : value);
}

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

// Mark cell to update here
static map_t markmap[MAX_MARKMAP_Y];

// Trace data cache, for faster redraw cells
#define TRACE_INDEX_COUNT (TRACES_MAX + STORED_TRACES)

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

static uint16_t trace_index_x[TRACE_INDEX_COUNT][SWEEP_POINTS_MAX];
static trace_coord_t trace_index_y[TRACE_INDEX_COUNT][SWEEP_POINTS_MAX];
_Static_assert(ARRAY_COUNT(trace_index_x[0]) == SWEEP_POINTS_MAX,
               "trace index x size mismatch");
_Static_assert(ARRAY_COUNT(trace_index_y[0]) == SWEEP_POINTS_MAX,
               "trace index y size mismatch");

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

static inline int float2int(float v) {
  return (int)(v + 0.5f);
}

//**************************************************************************************
// Plot area draw grid functions
//**************************************************************************************
static inline uint32_t squared_distance(int32_t x, int32_t y) {
  const int64_t dx = (int64_t)x * x;
  const int64_t dy = (int64_t)y * y;
  return (uint32_t)(dx + dy);
}

static bool polar_grid_point(int32_t x, int32_t y) {
  const uint32_t radius = P_RADIUS;
  const uint32_t radius_sq = (uint32_t)radius * radius;
  const uint32_t distance = squared_distance(x, y);
  if (distance > radius_sq + radius)
    return false;
  if (distance > radius_sq - radius)
    return true;
  if (x == 0 || y == 0)
    return true;
  const uint32_t radius_sq_1 = radius_sq / 25u;
  if (distance < radius_sq_1 - radius / 5u)
    return false;
  if (distance < radius_sq_1 + radius / 5u)
    return true;
  const uint32_t radius_sq_4 = (radius_sq * 4u) / 25u;
  if (distance < radius_sq_4 - (radius * 2u) / 5u)
    return false;
  if (distance < radius_sq_4 + (radius * 2u) / 5u)
    return true;
  if (x == y || x == -y)
    return true;
  const uint32_t radius_sq_9 = (radius_sq * 9u) / 25u;
  if (distance < radius_sq_9 - (radius * 3u) / 5u)
    return false;
  if (distance < radius_sq_9 + (radius * 3u) / 5u)
    return true;
  const uint32_t radius_sq_16 = (radius_sq * 16u) / 25u;
  if (distance < radius_sq_16 - (radius * 4u) / 5u)
    return false;
  return distance < radius_sq_16 + (radius * 4u) / 5u;
}

static bool smith_grid_point(int32_t x, int32_t y) {
  const uint32_t r = P_RADIUS;
  const uint32_t radius_sq = (uint32_t)r * r;
  uint32_t distance = squared_distance(x, y);
  if (distance > radius_sq + r)
    return false;
  if (distance > radius_sq - r)
    return true;
  if (y == 0)
    return true;
  if (y < 0)
    y = -y;
  const uint32_t r_y = r * (uint32_t)y;
  if (x >= 0) {
    if (x >= (int32_t)r / 2) {
      int32_t d = (int32_t)distance - (int32_t)(2u * r * (uint32_t)x + r_y) + (int32_t)radius_sq + (int32_t)r / 2;
      if (abs_u32(d) <= r)
        return true;
      d = (int32_t)distance - (int32_t)((3u * r / 2u) * (uint32_t)x) + (int32_t)radius_sq / 2 + (int32_t)r / 4;
      if (d >= 0 && (uint32_t)d <= r / 2u)
        return true;
    }
    int32_t d = (int32_t)distance - (int32_t)(2u * r * (uint32_t)x + 2u * r_y) + (int32_t)radius_sq + (int32_t)r;
    if (abs_u32(d) <= 2u * r)
      return true;
    d = (int32_t)distance - (int32_t)(r * (uint32_t)x) + (int32_t)r / 2;
    if (d >= 0 && (uint32_t)d <= r)
      return true;
  }
  int32_t d = (int32_t)distance - (int32_t)(2u * r * (uint32_t)x + 4u * r_y) + (int32_t)radius_sq + (int32_t)(2u * r);
  if (abs_u32(d) <= 4u * r)
    return true;
  d = (int32_t)distance - (int32_t)((r / 2u) * (uint32_t)x) - (int32_t)radius_sq / 2 + (int32_t)(3u * r / 4u);
  return abs_u32(d) <= (3u * r) / 2u;
}

static void render_polar_grid_cell(const RenderCellCtx* rcx, pixel_t color) {
  const int32_t base_x = (int32_t)rcx->x0 - P_CENTER_X;
  const int32_t base_y = (int32_t)rcx->y0 - P_CENTER_Y;
  for (uint16_t y = 0; y < rcx->h; ++y) {
    for (uint16_t x = 0; x < rcx->w; ++x) {
      if (polar_grid_point(base_x + x, base_y + y))
        *cell_ptr(rcx, x, y) = color;
    }
  }
}

static void render_smith_grid_cell(const RenderCellCtx* rcx, pixel_t color) {
  const int32_t base_x = (int32_t)rcx->x0 - P_CENTER_X;
  const int32_t base_y = (int32_t)rcx->y0 - P_CENTER_Y;
  for (uint16_t y = 0; y < rcx->h; ++y) {
    for (uint16_t x = 0; x < rcx->w; ++x) {
      if (smith_grid_point(base_x + x, base_y + y))
        *cell_ptr(rcx, x, y) = color;
    }
  }
}

static void render_admittance_grid_cell(const RenderCellCtx* rcx, pixel_t color) {
  const int32_t base_x = P_CENTER_X - (int32_t)rcx->x0;
  const int32_t base_y = (int32_t)rcx->y0 - P_CENTER_Y;
  for (uint16_t y = 0; y < rcx->h; ++y) {
    for (uint16_t x = 0; x < rcx->w; ++x) {
      if (smith_grid_point(-((int32_t)x) + base_x, base_y + y))
        *cell_ptr(rcx, x, y) = color;
    }
  }
}

#define GRID_BITS 7          // precision = 1 / 128
static uint16_t grid_offset; // .GRID_BITS fixed point value
static uint16_t grid_width;  // .GRID_BITS fixed point value

void update_grid(freq_t fstart, freq_t fstop) {
  uint32_t k, N = 4;
  freq_t fspan = fstop - fstart;
  if (fspan == 0) {
    grid_offset = grid_width = 0;
    return;
  }
  freq_t dgrid = 1000000000, grid; // Max grid step = pattern * 1GHz grid
  do {                             // Find appropriate grid step (1, 2, 5 pattern)
    grid = dgrid;
    k = fspan / grid;
    if (k >= N * 5) {
      grid *= 5;
      break;
    }
    if (k >= N * 2) {
      grid *= 2;
      break;
    }
    if (k >= N * 1) {
      grid *= 1;
      break;
    }
  } while (dgrid /= 10);
  // Calculate offset and grid width in pixel (use .GRID_BITS fixed point values)
  grid_offset = ((uint64_t)(fstart % grid) * (WIDTH << GRID_BITS)) / fspan;
  grid_width = ((uint64_t)grid * (WIDTH << GRID_BITS)) / fspan;
}

static inline int rectangular_grid_x(uint32_t x) {
  x -= CELLOFFSETX;
  if ((uint32_t)x > WIDTH)
    return 0;
  if (x == 0 || x == WIDTH)
    return 1;
  return (((x << GRID_BITS) + grid_offset) % grid_width) < (1 << GRID_BITS);
}

static inline int rectangular_grid_y(uint32_t y) {
  if ((uint32_t)y > HEIGHT)
    return 0;
  return (y % GRIDY) == 0;
}

/**
 * @brief Collect enabled trace types for the current sweep.
 *
 * @param[out] smith_is_impedance True when an impedance Smith chart is required.
 * @return Bitmask of enabled trace types.
 */
static uint32_t gather_trace_mask(bool* smith_is_impedance) {
  uint32_t trace_mask = 0;
  bool smith_impedance = false;
  for (int t = 0; t < TRACES_MAX; ++t) {
    if (!trace[t].enabled)
      continue;
    trace_mask |= (uint32_t)1u << trace[t].type;
    if (trace[t].type == TRC_SMITH && !ADMIT_MARKER_VALUE(trace[t].smith_format))
      smith_impedance = true;
  }
  if (smith_is_impedance)
    *smith_is_impedance = smith_impedance;
  return trace_mask;
}

/**
 * @brief Render rectangular grid lines for Cartesian plots.
 *
 * @param rcx   Cell render context.
 * @param color Grid line color.
 */
static void render_rectangular_grid_layer(RenderCellCtx* rcx, pixel_t color) {
  const uint16_t step = VNA_MODE(VNA_MODE_DOT_GRID) ? 2u : 1u;
  for (uint16_t x = 0; x < rcx->w; ++x) {
    if (!rectangular_grid_x(rcx->x0 + x))
      continue;
    for (uint16_t y = 0; y < rcx->h; y = (uint16_t)(y + step))
      *cell_ptr(rcx, x, y) = color;
  }
  for (uint16_t y = 0; y < rcx->h; ++y) {
    if (!rectangular_grid_y(rcx->y0 + y))
      continue;
    for (uint16_t x = 0; x < rcx->w; x = (uint16_t)(x + step)) {
      if ((uint32_t)(rcx->x0 + x - CELLOFFSETX) <= WIDTH)
        *cell_ptr(rcx, x, y) = color;
    }
  }
}

/**
 * @brief Render Smith or polar grids depending on active traces.
 *
 * @param rcx             Cell render context.
 * @param color           Grid line color.
 * @param trace_mask      Enabled trace mask.
 * @param smith_impedance True when impedance Smith chart is needed.
 */
static void render_round_grid_layer(RenderCellCtx* rcx, pixel_t color, uint32_t trace_mask,
                                    bool smith_impedance) {
  if (trace_mask & (1u << TRC_SMITH)) {
    if (smith_impedance)
      render_smith_grid_cell(rcx, color);
    else
      render_admittance_grid_cell(rcx, color);
    return;
  }
  if (trace_mask & (1u << TRC_POLAR))
    render_polar_grid_cell(rcx, color);
}

/**
 * @brief Compact cell buffer for clipped cells at the right edge.
 *
 * Copies only the visible columns to keep LCD DMA transfers contiguous.
 */
static void compact_cell_buffer(RenderCellCtx* rcx) {
  if (rcx->w >= CELLWIDTH)
    return;
  chDbgAssert(rcx->w <= CELLWIDTH, "invalid cell width");
  pixel_t* src = rcx->buf + CELLWIDTH;
  pixel_t* dst = rcx->buf + rcx->w;
  for (uint16_t row = 1; row < rcx->h; ++row) {
    for (uint16_t col = 0; col < rcx->w; ++col)
      *dst++ = *src++;
    src += CELLWIDTH - rcx->w;
  }
}

/**
 * @brief Render all traces that intersect the provided cell.
 *
 * Performs bounded searches on rectangular traces to avoid unnecessary work.
 */
static void render_traces_in_cell(RenderCellCtx* rcx) {
  if (sweep_points < 2)
    return;
  for (int t = TRACE_INDEX_COUNT - 1; t >= 0; --t) {
    if (!need_process_trace((uint16_t)t))
      continue;
    pixel_t color = GET_PALTETTE_COLOR(LCD_TRACE_1_COLOR + t);
    trace_index_const_table_t index = trace_index_const_table(t);
    bool rectangular = false;
    if (t < TRACES_MAX)
      rectangular = ((uint32_t)1u << trace[t].type) & RECTANGULAR_GRID_MASK;
    TraceIndexRange range = {.found = false, .i0 = 0, .i1 = 0};
    if (rectangular && !enabled_store_trace && sweep_points > 30) {
      range = search_index_range_x(rcx->x0, rcx->x0 + rcx->w, index);
    }
    uint16_t start = range.found ? range.i0 : 0u;
    uint16_t stop = range.found ? range.i1 : (uint16_t)(sweep_points - 1);
    uint16_t first_segment = (start > 0) ? (uint16_t)(start - 1) : 0u;
    uint16_t last_segment = (stop < (uint16_t)(sweep_points - 1)) ? (uint16_t)(stop + 1)
                                                                   : (uint16_t)(sweep_points - 1);
    if (last_segment <= first_segment)
      continue;
    for (uint16_t i = first_segment; i < last_segment; ++i) {
      int x1 = (int)TRACE_X(index, i) - rcx->x0;
      int y1 = (int)TRACE_Y(index, i) - rcx->y0;
      int x2 = (int)TRACE_X(index, i + 1) - rcx->x0;
      int y2 = (int)TRACE_Y(index, i + 1) - rcx->y0;
      cell_drawline(rcx, x1, y1, x2, y2, color);
    }
  }
}

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

/**
 * @brief Draw overlay information such as marker text, measurements, and references.
 */
static void render_overlays(RenderCellCtx* rcx) {
#if VNA_ENABLE_GRID_VALUES
  if (VNA_MODE(VNA_MODE_SHOW_GRID) && rcx->x0 > (GRID_X_TEXT - CELLWIDTH))
    cell_draw_grid_values(rcx);
#endif
  if (rcx->y0 <= marker_area_max())
    cell_draw_marker_info(rcx);
#ifdef __VNA_MEASURE_MODULE__
  cell_draw_measure(rcx);
#endif
  cell_draw_all_refpos(rcx);
}

//**************************************************************************************
// Cell render functions
//**************************************************************************************
#if VNA_FAST_RENDER
// Little faster on easy traces, 2x faster if need lot of clipping and draw long lines
// Bitmaps draw, 2x faster, but limit width <= 32
#include "../modules/vna_render.c"
#else
// Little slower on easy traces, but slow if need lot of clip and draw long lines
static inline void cell_drawline(const RenderCellCtx* rcx, int x0, int y0, int x1, int y1, pixel_t c) {
  if (x0 < 0 && x1 < 0)
    return;
  if (y0 < 0 && y1 < 0)
    return;
  if (x0 >= CELLWIDTH && x1 >= CELLWIDTH)
    return;
  if (y0 >= CELLHEIGHT && y1 >= CELLHEIGHT)
    return;

  // Modified Bresenham's line algorithm, see
  // https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm Draw from top to bottom (most graph
  // contain vertical lines)
  if (y1 < y0) {
    SWAP(int, x0, x1);
    SWAP(int, y0, y1);
  }
  int dx = (x0 - x1), sx = 1;
  if (dx > 0) {
    dx = -dx;
    sx = -sx;
  }
  int dy = (y1 - y0);
  int err = ((dy + dx) < 0 ? -dx : -dy) / 2;
  // Fast skip points while y0 < 0
  if (y0 < 0) {
    while (1) {
      int e2 = err;
      if (e2 > dx) {
        err -= dy;
        x0 += sx;
      }
      if (e2 < dy) {
        err -= dx;
        y0++;
        if (y0 == 0)
          break;
      }
    }
  }
  int y = y0;
  while (1) {
    if ((uint32_t)x0 < rcx->w && (uint32_t)y < rcx->h)
      *cell_ptr(rcx, (uint16_t)x0, (uint16_t)y) = c;
    if (x0 == x1 && y == y1)
      return;
    int e2 = err;
    if (e2 > dx) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dy) {
      err -= dx;
      ++y;
      if (y >= (int)rcx->h)
        return;
    } // stop after cell bottom
  }
}

// Slower, but allow any width bitmaps
static void cell_blit_bitmap(RenderCellCtx* rcx, int16_t x, int16_t y, uint16_t w, uint16_t h,
                             const uint8_t* bmp) {
  int16_t x1, y1;
  if ((x1 = x + w) < 0 || (y1 = y + h) < 0)
    return;
  if (y1 >= (int16_t)rcx->h)
    y1 = (int16_t)rcx->h; // clip bottom
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
      if ((uint32_t)(x + r) >= rcx->w)
        continue; // x+r < 0 || x+r >= CELLWIDTH
      *cell_ptr(rcx, (uint16_t)(x + r), (uint16_t)y) = foreground_color;
    }
  }
}
#endif

#ifdef VNA_ENABLE_SHADOW_TEXT
static void cell_blit_bitmap_shadow(RenderCellCtx* rcx, int16_t x, int16_t y, uint16_t w, uint16_t h,
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
static font_put_t put_char = put_normal;
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
static int cell_vprintf(RenderCellCtx* rcx, int16_t x, int16_t y, const char* fmt, va_list ap) {
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

static int cell_printf_ctx(RenderCellCtx* rcx, int16_t x, int16_t y, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int retval = cell_vprintf(rcx, x, y, fmt, ap);
  va_end(ap);
  return retval;
}

// Bound during draw_cell() so measurement helpers can reuse printf utilities.
static RenderCellCtx* active_cell_ctx = NULL;

static int cell_printf_bound(int16_t x, int16_t y, const char* fmt, ...) {
  chDbgAssert(active_cell_ctx != NULL, "No active cell context");
  va_list ap;
  va_start(ap, fmt);
  int retval = cell_vprintf(active_cell_ctx, x, y, fmt, ap);
  va_end(ap);
  return retval;
}

//**************************************************************************************
// Cell mark map functions
//**************************************************************************************
static void mark_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
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

/**
 * @brief Update cached trace coordinates and mark dirty cells when a segment moves.
 */
static void mark_set_index(trace_index_table_t index, uint16_t i, uint16_t x, uint16_t y,
                           MarkLineState* state) {
  chDbgAssert(i < SWEEP_POINTS_MAX, "index overflow");
  state->diff <<= 1;
  if (TRACE_X(index, i) != x || TRACE_Y(index, i) != y)
    state->diff |= 1u;
  if ((state->diff & 3u) != 0u && i > 0) {
    mark_line(state->last_x, state->last_y, TRACE_X(index, i), TRACE_Y(index, i));
    mark_line(TRACE_X(index, i - 1), TRACE_Y(index, i - 1), x, y);
  }
  state->last_x = TRACE_X(index, i);
  state->last_y = TRACE_Y(index, i);
  TRACE_X(index, i) = x;
  TRACE_Y(index, i) = y;
}

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
#ifdef __VNA_Z_RENORMALIZATION__
#define PORT_Z current_props._portz
#else
#define PORT_Z 50.0f
#endif
// Help functions
static float get_l(float re, float im) {
  return (re * re + im * im);
}
static float get_w(int i) {
  return 2 * VNA_PI * get_frequency(i);
}
static float get_s11_r(float re, float im, float z) {
  return vna_fabsf(2.0f * z * re / get_l(re, im) - z);
}
static float get_s21_r(float re, float im, float z) {
  return 1.0f * z * re / get_l(re, im) - z;
}
static float get_s11_x(float re, float im, float z) {
  return -2.0f * z * im / get_l(re, im);
}
static float get_s21_x(float re, float im, float z) {
  return -1.0f * z * im / get_l(re, im);
}

//**************************************************************************************
// LINEAR = |S|
//**************************************************************************************
static float linear(int i, const float* v) {
  (void)i;
  return vna_sqrtf(get_l(v[0], v[1]));
}

//**************************************************************************************
// LOGMAG = 20*log10f(|S|)
//**************************************************************************************
static float logmag(int i, const float* v) {
  (void)i;
  //  return log10f(get_l(v[0], v[1])) *  10.0f;
  //  return vna_logf(get_l(v[0], v[1])) * (10.0f / logf(10.0f));
  return vna_log10f_x_10(get_l(v[0], v[1]));
}

//**************************************************************************************
// PHASE angle in degree = atan2(im, re) * 180 / PI
//**************************************************************************************
static float phase(int i, const float* v) {
  (void)i;
  return (180.0f / VNA_PI) * vna_atan2f(v[1], v[0]);
}

//**************************************************************************************
// Group delay
//**************************************************************************************
static float groupdelay(const float* v, const float* w, uint32_t deltaf) {
  // atan(w)-atan(v) = atan((w-v)/(1+wv)), for complex v and w result q = v / w
  float r = w[0] * v[0] + w[1] * v[1];
  float i = w[0] * v[1] - w[1] * v[0];
  return vna_atan2f(i, r) / (2 * VNA_PI * deltaf);
}

//**************************************************************************************
// REAL
//**************************************************************************************
static float real(int i, const float* v) {
  (void)i;
  return v[0];
}

//**************************************************************************************
// IMAG
//**************************************************************************************
static float imag(int i, const float* v) {
  (void)i;
  return v[1];
}

//**************************************************************************************
// SWR = (1 + |S|)/(1 - |S|)
//**************************************************************************************
static float swr(int i, const float* v) {
  (void)i;
  float x = linear(i, v);
  if (x > 0.99f)
    return infinityf();
  return (1.0f + x) / (1.0f - x);
}

//**************************************************************************************
// Z parameters calculations from complex S
// Z = z0 * (1 + S) / (1 - S) = R + jX
// |Z| = sqrtf(R*R+X*X)
// Resolve this in complex give:
//   let S` = 1 - S  => re` = 1 - re and im` = -im
//       l` = re` * re` + im` * im`
// Z = z0 * (2 - S`) / S` = z0 * 2 / S` - z0
//  R = z0 * 2 * re` / l` - z0
//  X =-z0 * 2 * im` / l`
// |Z| = z0 * sqrt(4 * re / l` + 1)
// Z phase = atan(X, R)
//**************************************************************************************
static float resistance(int i, const float* v) {
  (void)i;
  return get_s11_r(1.0f - v[0], -v[1], PORT_Z);
}

static float reactance(int i, const float* v) {
  (void)i;
  return get_s11_x(1.0f - v[0], -v[1], PORT_Z);
}

static float mod_z(int i, const float* v) {
  (void)i;
  const float z0 = PORT_Z;
  return z0 * vna_sqrtf(get_l(1.0f + v[0], v[1]) / get_l(1.0f - v[0], v[1])); // always >= 0
}

static float phase_z(int i, const float* v) {
  (void)i;
  const float r = 1.0f - get_l(v[0], v[1]);
  const float x = 2.0f * v[1];
  return (180.0f / VNA_PI) * vna_atan2f(x, r);
}

//**************************************************************************************
// Use w = 2 * pi * frequency
// Get Series L and C from X
//  C = -1 / (w * X)
//  L =  X / w
//**************************************************************************************
static float series_c(int i, const float* v) {
  const float zi = reactance(i, v);
  const float w = get_w(i);
  return -1.0f / (w * zi);
}

static float series_l(int i, const float* v) {
  const float zi = reactance(i, v);
  const float w = get_w(i);
  return zi / w;
}

//**************************************************************************************
// Q factor = abs(X / R)
// Q = 2 * im / (1 - re * re - im * im)
//**************************************************************************************
static float qualityfactor(int i, const float* v) {
  (void)i;
  const float r = 1.0f - get_l(v[0], v[1]);
  const float x = 2.0f * v[1];
  return vna_fabsf(x / r);
}

//**************************************************************************************
// Y parameters (conductance and susceptance) calculations from complex S
// Y = (1 / z0) * (1 - S) / (1 + S) = G + jB
// Resolve this in complex give:
//   let S` = 1 + S  => re` = 1 + re and im` = im
//       l` = re` * re` + im` * im`
//      z0` = (1 / z0)
// Y = z0` * (2 - S`) / S` = 2 * z0` / S` - z0`
//  G =  2 * z0` * re` / l` - z0`
//  B = -2 * z0` * im` / l`
// |Y| = 1 / |Z|
//**************************************************************************************
static float conductance(int i, const float* v) {
  (void)i;
  return get_s11_r(1.0f + v[0], v[1], 1.0f / PORT_Z);
}

static float susceptance(int i, const float* v) {
  (void)i;
  return get_s11_x(1.0f + v[0], v[1], 1.0f / PORT_Z);
}

//**************************************************************************************
// Parallel R and X calculations from Y
// Rp = 1 / G
// Xp =-1 / B
//**************************************************************************************
static float parallel_r(int i, const float* v) {
  return 1.0f / conductance(i, v);
}

static float parallel_x(int i, const float* v) {
  return -1.0f / susceptance(i, v);
}

//**************************************************************************************
// Use w = 2 * pi * frequency
// Get Parallel L and C from B
//  C =  B / w
//  L = -1 / (w * B) = Xp / w
//**************************************************************************************
static float parallel_c(int i, const float* v) {
  const float yi = susceptance(i, v);
  const float w = get_w(i);
  return yi / w;
}

static float parallel_l(int i, const float* v) {
  const float xp = parallel_x(i, v);
  const float w = get_w(i);
  return xp / w;
}

static float mod_y(int i, const float* v) {
  return 1.0f / mod_z(i, v); // always >= 0
}

//**************************************************************************************
// S21 series and shunt
// S21 shunt  Z = 0.5f * z0 * S / (1 - S)
//   replace S` = (1 - S)
// S21 shunt  Z = 0.5f * z0 * (1 - S`) / S`
// S21 series Z = 2.0f * z0 * (1 - S ) / S
// Q21 = im / re
//**************************************************************************************
static float s21shunt_r(int i, const float* v) {
  (void)i;
  return get_s21_r(1.0f - v[0], -v[1], 0.5f * PORT_Z);
}

static float s21shunt_x(int i, const float* v) {
  (void)i;
  return get_s21_x(1.0f - v[0], -v[1], 0.5f * PORT_Z);
}

static float s21shunt_z(int i, const float* v) {
  (void)i;
  float l1 = get_l(v[0], v[1]);
  float l2 = get_l(1.0f - v[0], v[1]);
  return 0.5f * PORT_Z * vna_sqrtf(l1 / l2);
}

static float s21series_r(int i, const float* v) {
  (void)i;
  return get_s21_r(v[0], v[1], 2.0f * PORT_Z);
}

static float s21series_x(int i, const float* v) {
  (void)i;
  return get_s21_x(v[0], v[1], 2.0f * PORT_Z);
}

static float s21series_z(int i, const float* v) {
  (void)i;
  float l1 = get_l(v[0], v[1]);
  float l2 = get_l(1.0f - v[0], v[1]);
  return 2.0f * PORT_Z * vna_sqrtf(l2 / l1);
}

static float s21_qualityfactor(int i, const float* v) {
  (void)i;
  return vna_fabsf(v[1] / (v[0] - get_l(v[0], v[1])));
}

//**************************************************************************************
// Group delay
//**************************************************************************************
float groupdelay_from_array(int i, const float* v) {
  int bottom = (i == 0) ? 0 : -1;            // get prev point
  int top = (i == sweep_points - 1) ? 0 : 1; // get next point
  freq_t deltaf = get_sweep_frequency(ST_SPAN) / ((sweep_points - 1) / (top - bottom));
  return groupdelay(&v[2 * bottom], &v[2 * top], deltaf);
}

static inline void cartesian_scale(const float* v, int16_t* xp, int16_t* yp, float scale) {
  int16_t x = P_CENTER_X + float2int(v[0] * scale);
  int16_t y = P_CENTER_Y - float2int(v[1] * scale);
  if (x < CELLOFFSETX)
    x = CELLOFFSETX;
  else if (x > CELLOFFSETX + WIDTH)
    x = CELLOFFSETX + WIDTH;
  if (y < 0)
    y = 0;
  else if (y > HEIGHT)
    y = HEIGHT;
  *xp = x;
  *yp = y;
}

#if MAX_TRACE_TYPE != 30
#error "Redefined trace_type list, need check format_list"
#endif

const trace_info_t trace_info_list[MAX_TRACE_TYPE] = {
    // Type          name      format   delta format      symbol         ref   scale  get value
    [TRC_LOGMAG] = {"LOGMAG", "%.2f%s", S_DELTA "%.3f%s", S_dB, NGRIDY - 1, 10.0f, logmag},
    [TRC_PHASE] = {"PHASE", "%.2f%s", S_DELTA "%.2f%s", S_DEGREE, NGRIDY / 2, 90.0f, phase},
    [TRC_DELAY] = {"DELAY", "%.4F%s", "%.4F%s", S_SECOND, NGRIDY / 2, 1e-9f, groupdelay_from_array},
    [TRC_SMITH] = {"SMITH", NULL, NULL, "", 0, 1.00f, NULL}, // Custom
    [TRC_POLAR] = {"POLAR", NULL, NULL, "", 0, 1.00f, NULL}, // Custom
    [TRC_LINEAR] = {"LINEAR", "%.6f%s", S_DELTA "%.5f%s", "", 0, 0.125f, linear},
    [TRC_SWR] = {"SWR", "%.3f%s", S_DELTA "%.3f%s", "", 0, 0.25f, swr},
    [TRC_REAL] = {"REAL", "%.6f%s", S_DELTA "%.5f%s", "", NGRIDY / 2, 0.25f, real},
    [TRC_IMAG] = {"IMAG", "%.6fj%s", S_DELTA "%.5fj%s", "", NGRIDY / 2, 0.25f, imag},
    [TRC_R] = {"R", "%.3F%s", S_DELTA "%.3F%s", S_OHM, 0, 100.0f, resistance},
    [TRC_X] = {"X", "%.3F%s", S_DELTA "%.3F%s", S_OHM, NGRIDY / 2, 100.0f, reactance},
    [TRC_Z] = {"|Z|", "%.3F%s", S_DELTA "%.3F%s", S_OHM, 0, 50.0f, mod_z},
    [TRC_ZPHASE] = {"Z phase", "%.1f%s", S_DELTA "%.2f%s", S_DEGREE, NGRIDY / 2, 90.0f, phase_z},
    [TRC_G] = {"G", "%.3F%s", S_DELTA "%.3F%s", S_SIEMENS, 0, 0.01f, conductance},
    [TRC_B] = {"B", "%.3F%s", S_DELTA "%.3F%s", S_SIEMENS, NGRIDY / 2, 0.01f, susceptance},
    [TRC_Y] = {"|Y|", "%.3F%s", S_DELTA "%.3F%s", S_SIEMENS, 0, 0.02f, mod_y},
    [TRC_Rp] = {"Rp", "%.3F%s", S_DELTA "%.3F%s", S_OHM, 0, 100.0f, parallel_r},
    [TRC_Xp] = {"Xp", "%.3F%s", S_DELTA "%.3F%s", S_OHM, NGRIDY / 2, 100.0f, parallel_x},
    [TRC_Cs] = {"Cs", "%.4F%s", S_DELTA "%.4F%s", S_FARAD, NGRIDY / 2, 1e-8f, series_c},
    [TRC_Ls] = {"Ls", "%.4F%s", S_DELTA "%.4F%s", S_HENRY, NGRIDY / 2, 1e-8f, series_l},
    [TRC_Cp] = {"Cp", "%.4F%s", S_DELTA "%.4F%s", S_FARAD, NGRIDY / 2, 1e-8f, parallel_c},
    [TRC_Lp] = {"Lp", "%.4F%s", S_DELTA "%.4F%s", S_HENRY, NGRIDY / 2, 1e-8f, parallel_l},
    [TRC_Q] = {"Q", "%.4f%s", S_DELTA "%.3f%s", "", 0, 10.0f, qualityfactor},
    [TRC_Rser] = {"Rser", "%.3F%s", S_DELTA "%.3F%s", S_OHM, NGRIDY / 2, 100.0f, s21series_r},
    [TRC_Xser] = {"Xser", "%.3F%s", S_DELTA "%.3F%s", S_OHM, NGRIDY / 2, 100.0f, s21series_x},
    [TRC_Zser] = {"|Zser|", "%.3F%s", S_DELTA "%.3F%s", S_OHM, NGRIDY / 2, 100.0f, s21series_z},
    [TRC_Rsh] = {"Rsh", "%.3F%s", S_DELTA "%.3F%s", S_OHM, NGRIDY / 2, 100.0f, s21shunt_r},
    [TRC_Xsh] = {"Xsh", "%.3F%s", S_DELTA "%.3F%s", S_OHM, NGRIDY / 2, 100.0f, s21shunt_x},
    [TRC_Zsh] = {"|Zsh|", "%.3F%s", S_DELTA "%.3F%s", S_OHM, NGRIDY / 2, 100.0f, s21shunt_z},
    [TRC_Qs21] = {"Q", "%.4f%s", S_DELTA "%.3f%s", "", 0, 10.0f, s21_qualityfactor},
};

const marker_info_t marker_info_list[MS_END] = {
    // Type            name           format                        get real     get imag
    [MS_LIN] = {"LIN", "%.2f %+.1f" S_DEGREE, linear, phase},
    [MS_LOG] = {"LOG", "%.1f" S_dB " %+.1f" S_DEGREE, logmag, phase},
    [MS_REIM] = {"Re + Im", "%F%+jF", real, imag},
    [MS_RX] = {"R + jX", "%F%+jF" S_OHM, resistance, reactance},
    [MS_RLC] = {"R + L/C", "%F" S_OHM " %F%c", resistance, reactance}, // use LC calc for imag
    [MS_GB] = {"G + jB", "%F%+jF" S_SIEMENS, conductance, susceptance},
    [MS_GLC] = {"G + L/C", "%F" S_SIEMENS " %F%c", conductance, parallel_x}, // use LC calc for imag
    [MS_RpXp] = {"Rp + jXp", "%F%+jF" S_OHM, parallel_r, parallel_x},
    [MS_RpLC] = {"Rp + L/C", "%F" S_OHM " %F%c", parallel_r, parallel_x}, // use LC calc for imag
    [MS_SHUNT_RX] = {"R+jX SHUNT", "%F%+jF" S_OHM, s21shunt_r, s21shunt_x},
    [MS_SHUNT_RLC] = {"R+L/C SH..", "%F" S_OHM " %F%c", s21shunt_r,
                      s21shunt_x}, // use LC calc for imag
    [MS_SERIES_RX] = {"R+jX SERIES", "%F%+jF" S_OHM, s21series_r, s21series_x},
    [MS_SERIES_RLC] = {"R+L/C SER..", "%F" S_OHM " %F%c", s21series_r,
                       s21series_x}, // use LC calc for imag
};

const char* get_trace_typename(int t, int marker_smith_format) {
  if (t == TRC_SMITH && ADMIT_MARKER_VALUE(marker_smith_format))
    return "ADMIT";
  return trace_info_list[t].name;
}

const char* get_smith_format_names(int m) {
  return marker_info_list[m].name;
}

static void format_smith_value(RenderCellCtx* rcx, int xpos, int ypos, const float* coeff, uint16_t idx,
                               uint16_t m) {
  char value = 0;
  if (m >= MS_END)
    return;
  get_value_cb_t re = marker_info_list[m].get_re_cb;
  get_value_cb_t im = marker_info_list[m].get_im_cb;
  const char* format = marker_info_list[m].format;
  float zr = re(idx, coeff);
  float zi = im(idx, coeff);
  // Additional convert to L or C from zi for LC markers
  if (LC_MARKER_VALUE(m)) {
    float w = get_w(idx);
    if (zi < 0) {
      zi = -1.0f / (w * zi);
      value = S_FARAD[0];
    } // Capacity
    else {
      zi = zi / (w);
      value = S_HENRY[0];
    } // Inductive
  }
  cell_printf_ctx(rcx, xpos, ypos, format, zr, zi, value);
}

static void trace_print_value_string(RenderCellCtx* rcx, int xpos, int ypos, int t, int index,
                                     int index_ref) {
  // Check correct input
  uint8_t type = trace[t].type;
  if (type >= MAX_TRACE_TYPE)
    return;
  float (*array)[2] = measured[trace[t].channel];
  float* coeff = array[index];
  const char* format = index_ref >= 0 ? trace_info_list[type].dformat
                                      : trace_info_list[type].format; // Format string
  get_value_cb_t c = trace_info_list[type].get_value_cb;
  if (c) {                     // Run standard get value function from table
    float v = c(index, coeff); // Get value
    if (index_ref >= 0 && v != infinityf())
      v -= c(index, array[index_ref]); // Calculate delta value
    cell_printf_ctx(rcx, xpos, ypos, format, v, trace_info_list[type].symbol);
  } else { // Need custom marker format for SMITH / POLAR
    format_smith_value(rcx, xpos, ypos, coeff, index,
                       type == TRC_SMITH ? trace[t].smith_format : MS_REIM);
  }
}

static int trace_print_info(RenderCellCtx* rcx, int xpos, int ypos, int t) {
  float scale = get_trace_scale(t);
  const char* format;
  int type = trace[t].type;
  int smith = trace[t].smith_format;
  const char* v = trace_info_list[trace[t].type].symbol;
  switch (type) {
  case TRC_SMITH:
  case TRC_POLAR:
    format = (scale != 1.0f) ? "%s %0.1fFS" : "%s ";
    break;
  default:
    format = "%s %F%s/";
    break;
  }
  return cell_printf_ctx(rcx, xpos, ypos, format, get_trace_typename(type, smith), scale, v);
}

static float time_of_index(int idx) {
  freq_t span = get_sweep_frequency(ST_SPAN);
  return (idx * (sweep_points - 1)) / ((float)FFT_SIZE * span);
}

static float distance_of_index(int idx) {
  return velocity_factor * (SPEED_OF_LIGHT / 200.0f) * time_of_index(idx);
}

//**************************************************************************************
//                   Stored traces
//**************************************************************************************
#if STORED_TRACES > 0
static uint8_t enabled_store_trace = 0;
void toggle_stored_trace(int idx) {
  uint8_t mask = 1 << idx;
  if (enabled_store_trace & mask) {
    enabled_store_trace &= ~mask;
    request_to_redraw(REDRAW_AREA);
    return;
  }
  if (current_trace == TRACE_INVALID)
    return;
  memcpy(trace_index_x[TRACES_MAX + idx], trace_index_x[current_trace], sizeof(trace_index_x[0]));
  memcpy(trace_index_y[TRACES_MAX + idx], trace_index_y[current_trace], sizeof(trace_index_y[0]));
  enabled_store_trace |= mask;
}

uint8_t get_stored_traces(void) {
  return enabled_store_trace;
}

static bool need_process_trace(uint16_t idx) {
  if (idx < TRACES_MAX)
    return trace[idx].enabled;
  else if (idx < TRACE_INDEX_COUNT)
    return enabled_store_trace & (1 << (idx - TRACES_MAX));
  return false;
}
#else
#define enabled_store_trace 0
static bool need_process_trace(uint16_t idx) {
  return trace[idx].enabled;
}
#endif

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
static TraceIndexRange search_index_range_x(uint16_t x_start, uint16_t x_end,
                                            trace_index_const_table_t index) {
  TraceIndexRange range = {.found = false, .i0 = 0, .i1 = 0};
  if (sweep_points < 2)
    return range;
  if (x_end <= x_start)
    ++x_end;
  uint16_t head = 0;
  uint16_t tail = (uint16_t)(sweep_points - 1);
  uint16_t mid = 0;
  bool inside = false;
  for (uint8_t iter = 0; iter < 16; ++iter) {
    mid = (uint16_t)((head + tail) >> 1);
    uint16_t px = TRACE_X(index, mid);
    if (px >= x_end) {
      if (mid == tail)
        break;
      tail = mid;
    } else if (px < x_start) {
      if (mid == head)
        break;
      head = mid;
    } else {
      inside = true;
      break;
    }
  }
if (!inside) {
    uint16_t px_tail = TRACE_X(index, tail);
    uint16_t px_head = TRACE_X(index, head);
    if (px_tail >= x_start && px_tail < x_end) {
      mid = tail;
      inside = true;
    } else if (px_head >= x_start && px_head < x_end) {
      mid = head;
      inside = true;
    }
  }
  if (!inside)
    return range;
  uint16_t left = mid;
  while (left > 0 && TRACE_X(index, left - 1) >= x_start)
    --left;
  uint16_t right = mid;
  while (right + 1 < sweep_points && TRACE_X(index, right + 1) < x_end)
    ++right;
  range.found = true;
  range.i0 = left;
  range.i1 = right;
  return range;
}

//**************************************************************************************
//                  Marker text/marker plate functions
//**************************************************************************************
// Icons bitmap
#include "../resources/icons/icons_marker.c"

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
#include "../measurement/legacy_measure.c"
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
static void trace_into_index(int t) {
  uint16_t start = 0, stop = sweep_points - 1, i;
  float (*array)[2] = measured[trace[t].channel];
  trace_index_table_t index = trace_index_table(t);
  uint32_t type = 1 << trace[t].type;
  get_value_cb_t c =
      trace_info_list[trace[t].type].get_value_cb; // Get callback for value calculation
  float refpos = HEIGHT - (get_trace_refpos(t)) * GRIDY + 0.5f; // 0.5 for pixel align
  float scale = get_trace_scale(t);
  MarkLineState line_state = {0};
  if (type & RECTANGULAR_GRID_MASK) { // Run build for rect grid
    const float dscale = GRIDY / scale;
    if (type & (1 << TRC_SWR))
      refpos += dscale; // For SWR need shift value by 1.0 down
    uint32_t dx = ((WIDTH) << 16) / (sweep_points - 1),
             x = (CELLOFFSETX << 16) + dx * start + 0x8000;
    int32_t y;
    for (i = start; i <= stop; i++, x += dx) {
      float v = c ? c(i, array[i]) : 0.0f; // Get value
      if (v == infinityf()) {
        y = 0;
      } else {
        y = refpos - v * dscale;
        if (y < 0)
          y = 0;
        else if (y > HEIGHT)
          y = HEIGHT;
      }
      mark_set_index(index, i, (uint16_t)(x >> 16), (uint16_t)y, &line_state);
    }
    return;
  }
  // Smith/Polar grid
  if (type & ROUND_GRID_MASK) { // Need custom calculations
    const float rscale = P_RADIUS / scale;
    int16_t y, x;
    for (i = start; i <= stop; i++) {
      cartesian_scale(array[i], &x, &y, rscale);
      mark_set_index(index, i, (uint16_t)x, (uint16_t)y, &line_state);
    }
    return;
  }
}

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

#if VNA_ENABLE_GRID_VALUES
static void markmap_grid_values(void) {
  if (VNA_MODE(VNA_MODE_SHOW_GRID))
    invalidate_rect_px(GRID_X_TEXT, 0, LCD_WIDTH - OFFSETX, LCD_HEIGHT - 1);
}
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
  lcd_bulk_continue(OFFSETX + x0, OFFSETY + y0, rcx.w, rcx.h);
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
  if (vbat <= 0)
    return;
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
  request_to_redraw(REDRAW_PLOT | REDRAW_ALL);
  draw_all();
}
