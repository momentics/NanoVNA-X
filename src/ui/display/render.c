/*
 * NanoVNA-X cell rendering primitives
 */

#include "ui/display/render.h"
#include "chprintf.h"

//**************************************************************************************
// Cell render functions
//**************************************************************************************
#if VNA_FAST_RENDER
// Little faster on easy traces, 2x faster if need lot of clipping and draw long lines
// Bitmaps draw, 2x faster, but limit width <= 32
#include "fast_render/vna_render.c"
// Ensure non-static symbols are available if vna_render.c defines them as static inline or similar
// (Assuming vna_render.c provides compatible implementations)
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
    if ((uint32_t)x < rcx->w && (uint32_t)y < rcx->h)
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
void cell_set_font(int type) {
  put_char = type == FONT_SMALL ? put_small : put_normal;
}

#else
void cell_set_font(int type) {
  (void)type;
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

int cell_printf_ctx(RenderCellCtx* rcx, int16_t x, int16_t y, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int retval = cell_vprintf(rcx, x, y, fmt, ap);
  va_end(ap);
  return retval;
}

// Bound during draw_cell() so measurement helpers can reuse printf utilities.
static RenderCellCtx* active_cell_ctx = NULL;

void set_active_cell_ctx(RenderCellCtx* ctx) {
  active_cell_ctx = ctx;
}

int cell_printf_bound(int16_t x, int16_t y, const char* fmt, ...) {
  chDbgAssert(active_cell_ctx != NULL, "No active cell context");
  va_list ap;
  va_start(ap, fmt);
  int retval = cell_vprintf(active_cell_ctx, x, y, fmt, ap);
  va_end(ap);
  return retval;
}
