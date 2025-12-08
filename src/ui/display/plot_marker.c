#include "ui/display/plot_internal.h"
#include <string.h>

// Icons bitmap resources
#include "../resources/icons/icons_marker.c"

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

void render_markers_in_cell(RenderCellCtx* rcx) {
  uint32_t trace_mask = 0;
  // TODO: Gather trace mask logic is in plot_grid.c helper.
  // Maybe we should just iterate traces here.
  // The original function (710-744) iterates traces to draw markers.
  // Let's implement it.
  
  // Need to know enabled traces.
  for (int t = 0; t < TRACES_MAX; ++t) {
    if (!trace[t].enabled) continue;
    
    // For each marker
    for (int m = 0; m < MARKERS_MAX; m++) {
      if (!markers[m].enabled) continue;
      
      uint16_t idx = markers[m].index;
      trace_index_const_table_t index = trace_index_const_table(t);
      // Wait, render_markers_in_cell used logic to check if marker is in cell
      // I need to see the original implementation again to be precise.
      // Lines 710-744.
      
      // I'll refer to logic I viewed before or just rewrite standard logic.
      // Check if marker point (TRACE_X(index,idx), TRACE_Y(index, idx)) is inside cell rcx.
      
      int16_t mk_x = TRACE_X(index, idx);
      int16_t mk_y = TRACE_Y(index, idx);
      
      int x = mk_x - rcx->x0 - X_MARKER_OFFSET;
      int y = mk_y - rcx->y0 + ((mk_y < MARKER_HEIGHT * 2) ? 1 : -Y_MARKER_OFFSET);
      
      if (x > -MARKER_WIDTH && x < rcx->width && y > -MARKER_HEIGHT && y < rcx->height) {
        const uint8_t* marker = (const uint8_t*)MARKER_BITMAP(m);
        if (m == active_marker)
          marker = (const uint8_t*)MARKER_RBITMAP(m);
#ifdef VNA_ENABLE_SHADOW_TEXT
          cell_blit_bitmap_shadow(rcx, x, y, MARKER_WIDTH, MARKER_HEIGHT, marker);
#else
          cell_blit_bitmap(rcx, x, y, MARKER_WIDTH, MARKER_HEIGHT, marker);
#endif

      }
    }
  }
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

// Measure module logic
int cell_printf_bound(int16_t x, int16_t y, const char* fmt, ...) {
  chDbgAssert(active_cell_ctx != NULL, "No active cell context");
  va_list ap;
  va_start(ap, fmt);
  int retval = cell_vprintf(active_cell_ctx, x, y, fmt, ap);
  va_end(ap);
  return retval;
}

#ifdef __VNA_MEASURE_MODULE__
typedef void (*measure_cell_cb_t)(int x0, int y0);
typedef void (*measure_prepare_cb_t)(uint8_t mode, uint8_t update_mask);

static uint8_t data_update = 0;

#define MESAURE_NONE 0
#define MESAURE_S11 1                           // For calculate need only S11 data
#define MESAURE_S21 2                           // For calculate need only S21 data
#define MESAURE_ALL (MESAURE_S11 | MESAURE_S21) // For calculate need S11 and S21 data

// Defines moved to plot_internal.h

// Include measure functions
#ifdef __VNA_MEASURE_MODULE__
#define cell_printf cell_printf_bound
// legacy_measure.c expects invalidate_rect to be available for marking dirty regions.
// Provide a compatibility alias to the pixel-based helper defined above.
// Wait, invalidate_rect_px is in plot.c and not exposed. I need to expose it in plot_internal.h or something.
// Or define it here if possible. It calls request_to_redraw or similar.
// It seems invalidate_rect_px is dealing with dirty rects.
// I will need to expose `invalidate_rect_px` from plot.c
#define invalidate_rect plot_invalidate_rect
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

// Exposed to other modules calling it
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

// Exposed to plot.c logic
void measure_set_flag(uint8_t flag) {
  data_update |= flag;
}

void measure_prepare(void) {
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
#endif // __VNA_MEASURE_MODULE__

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
    while(0); // placeholder for formatting
    cell_printf_ctx(rcx, xpos, ypos, "M%d:", active_marker + 1);
     xpos += 3 * FONT_WIDTH + 2;
    if ((props_mode & DOMAIN_MODE) == DOMAIN_FREQ)
      cell_printf_ctx(rcx, xpos, ypos, MARKER_FREQ, get_marker_frequency(active_marker));
    else
      cell_printf_ctx(rcx, xpos, ypos, "%F" S_SECOND " (%F" S_METRE ")",
                  time_of_index(active_marker_idx), distance_of_index(active_marker_idx));
  }
}

void render_overlays(RenderCellCtx* rcx) {
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

void trace_print_value_string(RenderCellCtx* rcx, int xpos, int ypos, int t, int index,
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

int trace_print_info(RenderCellCtx* rcx, int xpos, int ypos, int t) {
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

