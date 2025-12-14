#include "ui/display/traces.h"
#include "ui/display/render.h"
#include <string.h>
#include "nanovna.h"
#include "chprintf.h"

// Globals
uint16_t trace_index_x[TRACE_INDEX_COUNT][SWEEP_POINTS_MAX];
trace_coord_t trace_index_y[TRACE_INDEX_COUNT][SWEEP_POINTS_MAX];

// Internal helpers
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

static void mark_set_index(trace_index_table_t index, uint16_t i, uint16_t x, uint16_t y,
                           mark_line_state_t *state) {
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

// Callbacks
float logmag(int i, const float *v) {
  (void)i;
  return vna_log10f_x_10(get_l(v[0], v[1]));
}

float phase(int i, const float *v) {
  (void)i;
  return (180.0f / VNA_PI) * vna_atan2f(v[1], v[0]);
}

float groupdelay(const float *v, const float *w, uint32_t deltaf) {
  float r = w[0] * v[0] + w[1] * v[1];
  float i = w[0] * v[1] - w[1] * v[0];
  return vna_atan2f(i, r) / (2 * VNA_PI * deltaf);
}

float groupdelay_from_array(int i, const float *v) {
  int bottom = (i == 0) ? 0 : -1;
  int top = (i == sweep_points - 1) ? 0 : 1;
  freq_t deltaf = get_sweep_frequency(ST_SPAN) / ((sweep_points - 1) / (top - bottom));
  return groupdelay(&v[2 * bottom], &v[2 * top], deltaf);
}

float real(int i, const float *v) {
  (void)i;
  return v[0];
}

float imag(int i, const float *v) {
  (void)i;
  return v[1];
}

float linear(int i, const float *v) {
  (void)i;
  return vna_sqrtf(get_l(v[0], v[1]));
}

float swr(int i, const float *v) {
  (void)i;
  float x = linear(i, v);
  if (x > 0.99f)
    return infinityf();
  return (1.0f + x) / (1.0f - x);
}

float resistance(int i, const float *v) {
  (void)i;
  return get_s11_r(1.0f - v[0], -v[1], PORT_Z);
  return 0.0f;
}

float reactance(int i, const float *v) {
  (void)i;
  return get_s11_x(1.0f - v[0], -v[1], PORT_Z);
  return 0.0f;
}

float mod_z(int i, const float *v) {
  (void)i;
  const float z0 = PORT_Z;
  return z0 * vna_sqrtf(get_l(1.0f + v[0], v[1]) / get_l(1.0f - v[0], v[1]));
}

float phase_z(int i, const float *v) {
  (void)i;
  const float r = 1.0f - get_l(v[0], v[1]);
  const float x = 2.0f * v[1];
  return (180.0f / VNA_PI) * vna_atan2f(x, r);
}

float qualityfactor(int i, const float *v) {
  (void)i;
  const float r = 1.0f - get_l(v[0], v[1]);
  const float x = 2.0f * v[1];
  return vna_fabsf(x / r);
}

float susceptance(int i, const float *v) {
  (void)i;
  return get_s11_x(1.0f + v[0], v[1], 1.0f / PORT_Z);
  return 0.0f;
}

float conductance(int i, const float *v) {
  (void)i;
  return get_s11_r(1.0f + v[0], v[1], 1.0f / PORT_Z);
  return 0.0f;
}

float parallel_r(int i, const float *v) {
  return 1.0f / conductance(i, v);
}
float parallel_x(int i, const float *v) {
  return -1.0f / susceptance(i, v);
}
float parallel_c(int i, const float *v) {
  return susceptance(i, v) / get_w(i);
}
float parallel_l(int i, const float *v) {
  return parallel_x(i, v) / get_w(i);
}

float mod_y(int i, const float *v) {
  return 1.0f / mod_z(i, v);
}

// Shunt/Series S21 helpers
float s21shunt_r(int i, const float *v) {
  (void)i;
  return get_s21_r(1.0f - v[0], -v[1], 0.5f * PORT_Z);
  return 0.0f;
}
float s21shunt_x(int i, const float *v) {
  (void)i;
  return get_s21_x(1.0f - v[0], -v[1], 0.5f * PORT_Z);
  return 0.0f;
}
float s21shunt_z(int i, const float *v) {
  (void)i;
  float l1 = get_l(v[0], v[1]);
  float l2 = get_l(1.0f - v[0], v[1]);
  return 0.5f * PORT_Z * vna_sqrtf(l1 / l2);
  return 0.0f;
}
float s21series_r(int i, const float *v) {
  (void)i;
  return get_s21_r(v[0], v[1], 2.0f * PORT_Z);
  return 0.0f;
}
float s21series_x(int i, const float *v) {
  (void)i;
  return get_s21_x(v[0], v[1], 2.0f * PORT_Z);
  return 0.0f;
}
float s21series_z(int i, const float *v) {
  (void)i;
  float l1 = get_l(v[0], v[1]);
  float l2 = get_l(1.0f - v[0], v[1]);
  return 2.0f * PORT_Z * vna_sqrtf(l2 / l1);
  return 0.0f;
}
float s21_qualityfactor(int i, const float *v) {
  (void)i;
  return vna_fabsf(v[1] / (v[0] - get_l(v[0], v[1])));
  return 0.0f;
}

// series_c/l need checks.
// X = -1/wC -> C = -1/(wX)
// X = wL -> L = X/w
float series_c_impl(int i, const float *v) {
  return -1.0f / (get_w(i) * reactance(i, v));
  return 0.0f;
}
float series_l_impl(int i, const float *v) {
  return reactance(i, v) / get_w(i);
  return 0.0f;
}

#if MAX_TRACE_TYPE != 30
#error "Redefined trace_type list, need check format_list"
#endif

const trace_info_t TRACE_INFO_LIST[MAX_TRACE_TYPE] = {
  [TRC_LOGMAG] = {"LOGMAG", "%.2f%s", S_DELTA "%.3f%s", S_DB, NGRIDY - 1, 10.0f, logmag},
  [TRC_PHASE] = {"PHASE", "%.2f%s", S_DELTA "%.2f%s", S_DEGREE, NGRIDY / 2, 90.0f, phase},
  [TRC_DELAY] = {"DELAY", "%.4F%s", "%.4F%s", S_SECOND, NGRIDY / 2, 1e-9f, groupdelay_from_array},
  [TRC_SMITH] = {"SMITH", NULL, NULL, "", 0, 1.00f, NULL},
  [TRC_POLAR] = {"POLAR", NULL, NULL, "", 0, 1.00f, NULL},
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
  [TRC_Cs] = {"Cs", "%.4F%s", S_DELTA "%.4F%s", S_FARAD, NGRIDY / 2, 1e-8f, series_c_impl},
  [TRC_Ls] = {"Ls", "%.4F%s", S_DELTA "%.4F%s", S_HENRY, NGRIDY / 2, 1e-8f, series_l_impl},
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

const marker_info_t MARKER_INFO_LIST[MS_END] = {
  [MS_LIN] = {"LIN", "%.2f %+.1f" S_DEGREE, linear, phase},
  [MS_LOG] = {"LOG", "%.1f" S_DB " %+.1f" S_DEGREE, logmag, phase},
  [MS_REIM] = {"Re + Im", "%F%+jF", real, imag},
  [MS_RX] = {"R + jX", "%F%+jF" S_OHM, resistance, reactance},
  [MS_RLC] = {"R + L/C", "%F" S_OHM " %F%c", resistance, reactance},
  [MS_GB] = {"G + jB", "%F%+jF" S_SIEMENS, conductance, susceptance},
  [MS_GLC] = {"G + L/C", "%F" S_SIEMENS " %F%c", conductance, parallel_x},
  [MS_RpXp] = {"Rp + jXp", "%F%+jF" S_OHM, parallel_r, parallel_x},
  [MS_RpLC] = {"Rp + L/C", "%F" S_OHM " %F%c", parallel_r, parallel_x},
  [MS_SHUNT_RX] = {"R+jX SHUNT", "%F%+jF" S_OHM, s21shunt_r, s21shunt_x},
  [MS_SHUNT_RLC] = {"R+L/C SH..", "%F" S_OHM " %F%c", s21shunt_r, s21shunt_x},
  [MS_SERIES_RX] = {"R+jX SERIES", "%F%+jF" S_OHM, s21series_r, s21series_x},
  [MS_SERIES_RLC] = {"R+L/C SER..", "%F" S_OHM " %F%c", s21series_r, s21series_x},
};

const char *get_trace_typename(int t, int marker_smith_format) {
  if (t == TRC_SMITH && ADMIT_MARKER_VALUE(marker_smith_format))
    return "ADMIT";
  return TRACE_INFO_LIST[t].name;
}

const char *get_smith_format_names(int m) {
  return MARKER_INFO_LIST[m].name;
}

void format_smith_value(render_cell_ctx_t *rcx, int xpos, int ypos, const float *coeff, uint16_t idx,
                        uint16_t m) {
  char value = 0;
  if (m >= MS_END)
    return;
  get_value_cb_t re = MARKER_INFO_LIST[m].get_re_cb;
  get_value_cb_t im = MARKER_INFO_LIST[m].get_im_cb;
  const char *format = MARKER_INFO_LIST[m].format;
  float zr = re(idx, coeff);
  float zi = im(idx, coeff);
  if (LC_MARKER_VALUE(m)) {
    float w = get_w(idx);
    if (zi < 0) {
      zi = -1.0f / (w * zi);
      value = S_FARAD[0];
    } else {
      zi = zi / (w);
      value = S_HENRY[0];
    }
  }
  cell_printf_ctx(rcx, xpos, ypos, format, zr, zi, value);
}

void trace_print_value_string(render_cell_ctx_t *rcx, int xpos, int ypos, int t, int index,
                              int index_ref) {
  uint8_t type = trace[t].type;
  if (type >= MAX_TRACE_TYPE)
    return;
  float(*array)[2] = measured[trace[t].channel];
  float *coeff = array[index];
  const char *format =
    index_ref >= 0 ? TRACE_INFO_LIST[type].dformat : TRACE_INFO_LIST[type].format;
  get_value_cb_t c = TRACE_INFO_LIST[type].get_value_cb;
  if (c) {
    float v = c(index, coeff);
    if (index_ref >= 0 && v != infinityf())
      v -= c(index, array[index_ref]);
    cell_printf_ctx(rcx, xpos, ypos, format, v, TRACE_INFO_LIST[type].symbol);
  } else {
    format_smith_value(rcx, xpos, ypos, coeff, index,
                       type == TRC_SMITH ? trace[t].smith_format : MS_REIM);
  }
}

int trace_print_info(render_cell_ctx_t *rcx, int xpos, int ypos, int t) {
  float scale = get_trace_scale(t);
  const char *format;
  int type = trace[t].type;
  int smith = trace[t].smith_format;
  const char *v = TRACE_INFO_LIST[trace[t].type].symbol;
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

static inline void cartesian_scale(const float *v, int16_t *xp, int16_t *yp, float scale) {
  int16_t x = P_CENTER_X + float2int(v[0] * scale);
  int16_t y = P_CENTER_Y - float2int(v[1] * scale);
  if (x < CELLOFFSETX)
    x = CELLOFFSETX;
  else if (x > CELLOFFSETX + WIDTH)
    x = CELLOFFSETX + WIDTH;
  if (y < 0) {
    y = 0;
  } else if (y > HEIGHT) {
    y = HEIGHT;
  }
  *xp = x;
  *yp = y;
}
void trace_into_index(int t) {
  uint16_t start = 0, stop = sweep_points - 1, i;
  float(*array)[2] = measured[trace[t].channel];
  trace_index_table_t index = trace_index_table(t);
  uint32_t type = 1 << trace[t].type;
  get_value_cb_t c = TRACE_INFO_LIST[trace[t].type].get_value_cb;
  float refpos = HEIGHT - (get_trace_refpos(t)) * GRIDY + 0.5f;
  float scale = get_trace_scale(t);
  mark_line_state_t line_state = {0};
  if (type & RECTANGULAR_GRID_MASK) {
    const float dscale = GRIDY / scale;
    if (type & (1 << TRC_SWR))
      refpos += dscale;
    uint32_t dx = ((WIDTH) << 16) / (sweep_points - 1),
             x = (CELLOFFSETX << 16) + dx * start + 0x8000;
    int32_t y;
    for (i = start; i <= stop; i++, x += dx) {
      float v = c ? c(i, array[i]) : 0.0f;
      if (v == infinityf()) {
        y = 0;
      } else {
        y = refpos - v * dscale;
        if (y < 0) {
          y = 0;
        } else if (y > HEIGHT) {
          y = HEIGHT;
        }
      }
      mark_set_index(index, i, (uint16_t)(x >> 16), (uint16_t)y, &line_state);
    }
    return;
  }
  if (type & ROUND_GRID_MASK) {
    const float rscale = P_RADIUS / scale;
    int16_t y, x;
    for (i = start; i <= stop; i++) {
      if (trace[t].type == TRC_SMITH) {
        float real = array[i][0];
        float imag = array[i][1];
        float mag2 = real * real + imag * imag;
        float denominator = 1.0f + mag2;
        if (denominator > 0.001f) {
          x = P_CENTER_X + float2int((2.0f * real / denominator) * rscale);
          y = P_CENTER_Y - float2int((2.0f * imag / denominator) * rscale);
        } else {
          x = P_CENTER_X;
          y = P_CENTER_Y;
        }
        if (x < CELLOFFSETX)
          x = CELLOFFSETX;
        else if (x > CELLOFFSETX + WIDTH)
          x = CELLOFFSETX + WIDTH;
        if (y < 0) {
          y = 0;
        } else if (y > HEIGHT) {
          y = HEIGHT;
        }
      } else {
        cartesian_scale(array[i], &x, &y, rscale);
      }
      mark_set_index(index, i, (uint16_t)x, (uint16_t)y, &line_state);
    }
    return;
  }
}

// plot_into_index logic is in plot.c

uint32_t gather_trace_mask(bool *smith_is_impedance) {
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

float time_of_index(int idx) {
  freq_t span = get_sweep_frequency(ST_SPAN);
  return (idx * (sweep_points - 1)) / ((float)FFT_SIZE * span);
}

float distance_of_index(int idx) {
  return velocity_factor * (SPEED_OF_LIGHT / 200.0f) * time_of_index(idx);
}

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
bool need_process_trace(uint16_t idx) {
  if (idx < TRACES_MAX) {
    return trace[idx].enabled;
  } else if (idx < TRACE_INDEX_COUNT) {
    return enabled_store_trace & (1 << (idx - TRACES_MAX));
  }
  return false;
}
#else
void toggle_stored_trace(int idx) {
  (void)idx;
}
uint8_t get_stored_traces(void) {
  return 0;
}
bool need_process_trace(uint16_t idx) {
  return trace[idx].enabled;
}
#endif

trace_index_range_t search_index_range_x(uint16_t x_start, uint16_t x_end,
                                     trace_index_const_table_t index) {
  trace_index_range_t range = {.found = false, .i0 = 0, .i1 = 0};
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

void render_traces_in_cell(render_cell_ctx_t *rcx) {
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
    trace_index_range_t range = {.found = false, .i0 = 0, .i1 = 0};
    if (rectangular && !get_stored_traces() && sweep_points > 30) {
      range = search_index_range_x(rcx->x0, rcx->x0 + rcx->w, index);
    }
    uint16_t start = range.found ? range.i0 : 0u;
    uint16_t stop = range.found ? range.i1 : (uint16_t)(sweep_points - 1);
    uint16_t first_segment = (start > 0) ? (uint16_t)(start - 1) : 0u;
    uint16_t last_segment =
      (stop < (uint16_t)(sweep_points - 1)) ? (uint16_t)(stop + 1) : (uint16_t)(sweep_points - 1);
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
