#ifndef UI_DISPLAY_PLOT_INTERNAL_H
#define UI_DISPLAY_PLOT_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include "nanovna.h"

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

// PORT_Z definition moved from plot.c
#ifdef __VNA_Z_RENORMALIZATION__
#define PORT_Z current_props._portz
#else
#define PORT_Z 50.0f
#endif

// Helper functions (formerly static inline in plot.c)

static inline uint16_t clamp_u16(int value, uint16_t min_value, uint16_t max_value) {
  if (value < (int)min_value)
    return min_value;
  if (value > (int)max_value)
    return max_value;
  return (uint16_t)value;
}

static inline pixel_t* cell_ptr(const RenderCellCtx* rcx, uint16_t x, uint16_t y) {
  return rcx->buf + (uint32_t)y * CELLWIDTH + x;
}

static inline void cell_clear(RenderCellCtx* rcx, pixel_t color) {
  // chDbgAssert(((uintptr_t)rcx->buf % sizeof(uint32_t)) == 0, "cell buffer must be 32-bit aligned");
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

// Shared globals (defined in plot.c)
extern map_t markmap[MAX_MARKMAP_Y];

/**
 * @brief Create a horizontal bitmask covering the inclusive column range.
 */
static inline map_t markmap_mask(uint16_t x_begin, uint16_t x_end) {
  if (x_begin >= MAX_MARKMAP_X)
    return 0;
  if (x_end >= MAX_MARKMAP_X)
    x_end = MAX_MARKMAP_X - 1;
  map_t m = 0;
  // Create mask for range [x_begin, x_end]
  // 1. Create mask for bits up to x_end (inclusive)
  if (x_end < sizeof(map_t) * 8 - 1)
    m = ((map_t)1 << (x_end + 1)) - 1;
  else
    m = (map_t)-1;
  // 2. Clear bits before x_begin
  if (x_begin > 0)
    m &= ~(((map_t)1 << x_begin) - 1);
  return m;
}

// Trace index count define
#define TRACE_INDEX_COUNT (TRACES_MAX + STORED_TRACES)

static inline int float2int(float v) {
  return (int)(v + 0.5f);
}

static inline uint32_t squared_distance(int32_t x, int32_t y) {
  const int64_t dx = (int64_t)x * x;
  const int64_t dy = (int64_t)y * y;
  return (uint32_t)(dx + dy);
}

// Math helpers moved from plot.c
static inline float get_l(float re, float im) {
  return re * re + im * im;
}

static inline float get_s11_r(float re, float im, float z) {
  float l = get_l(re, im);
  return z * (1.0f - l) / (1.0f - 2.0f * re + l);
}

static inline float get_s11_x(float re, float im, float z) {
  return -2.0f * z * im / (1.0f - 2.0f * re + get_l(re, im));
}

static inline float get_s21_r(float re, float im, float z) {
  float l = get_l(re, im);
  return z * (1.0f - l) / l;
}

static inline float get_s21_x(float re, float im, float z) {
  return -2.0f * z * im / get_l(re, im);
}

static inline float get_w(int i) { return 2 * VNA_PI * get_sweep_frequency(ST_START + i); }

#endif // UI_DISPLAY_PLOT_INTERNAL_H
