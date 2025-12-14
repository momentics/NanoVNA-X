#ifndef UI_DISPLAY_RENDER_H
#define UI_DISPLAY_RENDER_H

#include "ui/display/plot_internal.h"
#include <stdarg.h>

void cell_drawline(const render_cell_ctx_t *rcx, int x0, int y0, int x1, int y1, pixel_t c);
void cell_blit_bitmap(render_cell_ctx_t *rcx, int16_t x, int16_t y, uint16_t w, uint16_t h,
                      const uint8_t *bmp);

#if VNA_ENABLE_SHADOW_TEXT
void cell_blit_bitmap_shadow(render_cell_ctx_t *rcx, int16_t x, int16_t y, uint16_t w, uint16_t h,
                             const uint8_t *bmp);
#endif

void cell_set_font(int type);
int cell_printf_ctx(render_cell_ctx_t *rcx, int16_t x, int16_t y, const char *fmt, ...);
int cell_printf_bound(int16_t x, int16_t y, const char *fmt, ...);

void set_active_cell_ctx(render_cell_ctx_t *ctx);

#endif // UI_DISPLAY_RENDER_H
