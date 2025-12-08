#ifndef __UI_DISPLAY_PLOT_INTERNAL_H__
#define __UI_DISPLAY_PLOT_INTERNAL_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "nanovna.h"

#define float2int(x) ((int)(x))

static inline uint32_t abs_u32(int32_t x) {
  return (uint32_t)((x < 0) ? -x : x);
}

// Cell render use spi buffer
/**
 * @brief Rendering context for a single LCD cell.
 *
 * This structure holds state for the cell currently being rendered by the DMA engine.
 * The buffer `buf` is a direct pointer to the SPI/I2S DMA memory.
 * x0, y0 are absolute screen coordinates of the cell's top-left corner.
 */
typedef struct {
  uint16_t x0;      ///< Absolute X coordinate of the cell's left edge
  uint16_t y0;      ///< Absolute Y coordinate of the cell's top edge
  uint16_t width;   ///< Width of the cell in pixels
  uint16_t height;  ///< Height of the cell in pixels
  pixel_t* buf;     ///< Pointer to the display buffer (DMA memory)
} RenderCellCtx;

extern RenderCellCtx* active_cell_ctx;

static inline RenderCellCtx render_cell_ctx(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h, pixel_t* buffer) {
  RenderCellCtx rcx;
  rcx.x0 = x0;
  rcx.y0 = y0;
  rcx.width = w;
  rcx.height = h;
  rcx.buf = buffer;
  return rcx;
}

static inline pixel_t* cell_ptr(const RenderCellCtx* rcx, uint16_t x, uint16_t y) {
  return &rcx->buf[y * CELLWIDTH + x];
}

static inline void cell_clear(RenderCellCtx* rcx, pixel_t color) {
#if LCD_PIXEL_SIZE == 2
  const uint32_t packed = (uint32_t)color | ((uint32_t)color << 16);
  const size_t stride32 = CELLWIDTH / 2;
#elif LCD_PIXEL_SIZE == 1
  const uint32_t packed = (uint32_t)color | ((uint32_t)color << 8) | ((uint32_t)color << 16) |
                          ((uint32_t)color << 24);
  const size_t stride32 = CELLWIDTH / 4;
#else
#error "Unsupported LCD pixel size"
#endif
  uint32_t* dst32 = (uint32_t*)rcx->buf;
  for (size_t y = 0; y < rcx->height; ++y) {
    for (size_t x = 0; x < stride32; ++x)
      dst32[x] = packed;
    dst32 += stride32;
  }
}

// Shared drawing primitives
void cell_drawline(const RenderCellCtx* rcx, int x0, int y0, int x1, int y1, pixel_t c);
void cell_blit_bitmap(RenderCellCtx* rcx, int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t* bmp);
void cell_blit_bitmap_shadow(RenderCellCtx* rcx, int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t* bmp);
int cell_printf_ctx(RenderCellCtx* rcx, int16_t x, int16_t y, const char* fmt, ...);
int cell_vprintf(RenderCellCtx* rcx, int16_t x, int16_t y, const char* fmt, va_list ap);
// pixel_t cell_ptr(const RenderCellCtx* rcx, uint16_t x, uint16_t y); // moved inline
// void cell_clear(RenderCellCtx* rcx, pixel_t color); // moved inline
uint32_t squared_distance(int32_t x, int32_t y);

int rectangular_grid_x(uint32_t x);
int rectangular_grid_y(uint32_t y);

// plot_grid.c
void render_rectangular_grid_layer(RenderCellCtx* rcx, pixel_t color);
void render_round_grid_layer(RenderCellCtx* rcx, pixel_t color, uint32_t trace_mask, bool smith_impedance);
void update_grid(freq_t fstart, freq_t fstop);
void cell_draw_grid_values(RenderCellCtx* rcx);

// plot_trace.c
void render_traces_in_cell(RenderCellCtx* rcx);
void toggle_stored_trace(int idx);
uint8_t get_stored_traces(void);
bool need_process_trace(uint16_t idx);
typedef struct {
  const int16_t* x;
  const int16_t* y;
} trace_index_const_table_t;
typedef struct {
  int16_t* x;
  int16_t* y;
} trace_index_table_t;
trace_index_table_t trace_index_table(int trace_id);
trace_index_const_table_t trace_index_const_table(int trace_id);

#define TRACE_X(table, idx) ((table).x[(idx)])
#define TRACE_Y(table, idx) ((table).y[(idx)])

// plot_marker.c
void render_markers_in_cell(RenderCellCtx* rcx);
void render_overlays(RenderCellCtx* rcx);
const char* plot_get_measure_cal_txt(void);

// Shared plot_trace.c exports
// Structs trace_info_t etc defined in vna_types.h (via nanovna.h)
// lists extern declared in nanovna.h

const char* get_trace_typename(int t, int marker_smith_format);
const char* get_smith_format_names(int m);
float time_of_index(int idx);
float distance_of_index(int idx);
float groupdelay_from_array(int i, const float* v);

// plot_marker.c additional exports
int trace_print_info(RenderCellCtx* rcx, int xpos, int ypos, int t);
void trace_print_value_string(RenderCellCtx* rcx, int xpos, int ypos, int t, int index, int index_ref);

// plot_trace.c additional exports
void trace_into_index(int t);
void marker_search(void);
void marker_search_dir(int16_t from, int16_t dir);
int search_nearest_index(int x, int y, int t);

// plot.c exports
void plot_mark_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void plot_invalidate_rect(int x0, int y0, int x1, int y1);




#define MEASURE_UPD_SWEEP (1 << 0) // Recalculate on sweep done
#define MEASURE_UPD_FREQ (1 << 1)  // Recalculate on marker change position
#define MEASURE_UPD_ALL (MEASURE_UPD_SWEEP | MEASURE_UPD_FREQ)

#ifdef __VNA_Z_RENORMALIZATION__
#define PORT_Z current_props._portz
#else
#define PORT_Z 50.0f
#endif

void measure_prepare(void);
void measure_set_flag(uint8_t flag);
uint32_t gather_trace_mask(bool* smith_impedance);

void markmap_all_markers(void);
float get_l(float re, float im);
float get_w(int i);
float linear(int i, const float* v);
float logmag(int i, const float* v);
float phase(int i, const float* v);
float groupdelay(const float* v, const float* w, uint32_t deltaf);
float real(int i, const float* v);
float imag(int i, const float* v);
float swr(int i, const float* v);
float resistance(int i, const float* v);
float reactance(int i, const float* v);
float mod_z(int i, const float* v);
float phase_z(int i, const float* v);
float series_c(int i, const float* v);
float series_l(int i, const float* v);
float qualityfactor(int i, const float* v);
float conductance(int i, const float* v);
float susceptance(int i, const float* v);
float parallel_r(int i, const float* v);
float parallel_x(int i, const float* v);
float parallel_c(int i, const float* v);
float parallel_l(int i, const float* v);
float mod_y(int i, const float* v);
float s21series_r(int i, const float* v);
float s21series_x(int i, const float* v);
float s21series_z(int i, const float* v);
float s21shunt_r(int i, const float* v);
float s21shunt_x(int i, const float* v);
float s21shunt_z(int i, const float* v);
float s21_qualityfactor(int i, const float* v);

#endif // __UI_DISPLAY_PLOT_INTERNAL_H__
