#include "ui/display/plot_internal.h"
#include <string.h>

// Trace data cache, for faster redraw cells
#define TRACE_INDEX_COUNT (TRACES_MAX + STORED_TRACES)
_Static_assert(TRACE_INDEX_COUNT > 0, "Trace index count must be positive");

#if HEIGHT > UINT8_MAX
typedef uint16_t trace_coord_t;
#else
typedef uint8_t trace_coord_t;
#endif

// We need these struct definitions here as they are implementation details of the trace storage
// The implementation in plot_internal.h was forward declaration only via typedef? 
// No, I put typedefs in plot_internal.h, but here we need the full struct definition matching those typedefs.
// Actually, in C you can't redefine typedefs easily.
// Let's rely on the types defined in plot_internal.h.
// Wait, in plot_internal.h I defied:
// typedef struct { const int16_t* x; const int16_t* y; } trace_index_const_table_t;
// But here the arrays are static.

static uint16_t trace_index_x[TRACE_INDEX_COUNT][SWEEP_POINTS_MAX];
static trace_coord_t trace_index_y[TRACE_INDEX_COUNT][SWEEP_POINTS_MAX];
_Static_assert(ARRAY_COUNT(trace_index_x[0]) == SWEEP_POINTS_MAX,
               "trace index x size mismatch");
_Static_assert(ARRAY_COUNT(trace_index_y[0]) == SWEEP_POINTS_MAX,
               "trace index y size mismatch");

// Macros moved to plot_internal.h

trace_index_table_t trace_index_table(int trace_id) {
  trace_index_table_t table = {(int16_t*)trace_index_x[trace_id], (int16_t*)trace_index_y[trace_id]};
  return table;
}

trace_index_const_table_t trace_index_const_table(int trace_id) {
  trace_index_const_table_t table = {(int16_t*)trace_index_x[trace_id], (int16_t*)trace_index_y[trace_id]};
  return table;
}

typedef struct {
  bool found;
  uint16_t i0;
  uint16_t i1;
} TraceIndexRange;

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
  if (idx < TRACES_MAX)
    return trace[idx].enabled;
  else if (idx < TRACE_INDEX_COUNT)
    return enabled_store_trace & (1 << (idx - TRACES_MAX));
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

void render_traces_in_cell(RenderCellCtx* rcx) {
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
    if (rectangular && !get_stored_traces() && sweep_points > 30) {
       // Note: get_stored_traces() check used enabled_store_trace variable directly before.
       // Using getter is fine.
      range = search_index_range_x(rcx->x0, rcx->x0 + rcx->width, index);
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
 * @brief Tracks state transitions when recomputing trace sample positions.
 */
typedef struct {
  uint16_t diff;
  uint16_t last_x;
  uint16_t last_y;
} MarkLineState;
float get_l(float re, float im) {
  return (re * re + im * im);
}
float get_w(int i) {
  return 2 * VNA_PI * get_frequency(i);
}
float get_s11_r(float re, float im, float z) {
  return vna_fabsf(2.0f * z * re / get_l(re, im) - z);
}
float get_s21_r(float re, float im, float z) {
  return 1.0f * z * re / get_l(re, im) - z;
}
float get_s11_x(float re, float im, float z) {
  return -2.0f * z * im / get_l(re, im);
}
float get_s21_x(float re, float im, float z) {
  return -1.0f * z * im / get_l(re, im);
}

//**************************************************************************************
// LINEAR = |S|
//**************************************************************************************
float linear(int i, const float* v) {
  (void)i;
  return vna_sqrtf(get_l(v[0], v[1]));
}

//**************************************************************************************
// LOGMAG = 20*log10f(|S|)
//**************************************************************************************
float logmag(int i, const float* v) {
  (void)i;
  //  return log10f(get_l(v[0], v[1])) *  10.0f;
  //  return vna_logf(get_l(v[0], v[1])) * (10.0f / logf(10.0f));
  return vna_log10f_x_10(get_l(v[0], v[1]));
}

//**************************************************************************************
// PHASE angle in degree = atan2(im, re) * 180 / PI
//**************************************************************************************
float phase(int i, const float* v) {
  (void)i;
  return (180.0f / VNA_PI) * vna_atan2f(v[1], v[0]);
}

//**************************************************************************************
// Group delay
//**************************************************************************************
float groupdelay(const float* v, const float* w, uint32_t deltaf) {
  // atan(w)-atan(v) = atan((w-v)/(1+wv)), for complex v and w result q = v / w
  float r = w[0] * v[0] + w[1] * v[1];
  float i = w[0] * v[1] - w[1] * v[0];
  return vna_atan2f(i, r) / (2 * VNA_PI * deltaf);
}

//**************************************************************************************
// REAL
//**************************************************************************************
float real(int i, const float* v) {
  (void)i;
  return v[0];
}

//**************************************************************************************
// IMAG
//**************************************************************************************
float imag(int i, const float* v) {
  (void)i;
  return v[1];
}

//**************************************************************************************
// SWR = (1 + |S|)/(1 - |S|)
//**************************************************************************************
float swr(int i, const float* v) {
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
float resistance(int i, const float* v) {
  (void)i;
  return get_s11_r(1.0f - v[0], -v[1], PORT_Z);
}

float reactance(int i, const float* v) {
  (void)i;
  return get_s11_x(1.0f - v[0], -v[1], PORT_Z);
}

float mod_z(int i, const float* v) {
  (void)i;
  const float z0 = PORT_Z;
  return z0 * vna_sqrtf(get_l(1.0f + v[0], v[1]) / get_l(1.0f - v[0], v[1])); // always >= 0
}

float phase_z(int i, const float* v) {
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
float series_c(int i, const float* v) {
  const float zi = reactance(i, v);
  const float w = get_w(i);
  return -1.0f / (w * zi);
}

float series_l(int i, const float* v) {
  const float zi = reactance(i, v);
  const float w = get_w(i);
  return zi / w;
}

//**************************************************************************************
// Q factor = abs(X / R)
// Q = 2 * im / (1 - re * re - im * im)
//**************************************************************************************
float qualityfactor(int i, const float* v) {
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
float conductance(int i, const float* v) {
  (void)i;
  return get_s11_r(1.0f + v[0], v[1], 1.0f / PORT_Z);
}

float susceptance(int i, const float* v) {
  (void)i;
  return get_s11_x(1.0f + v[0], v[1], 1.0f / PORT_Z);
}

//**************************************************************************************
// Parallel R and X calculations from Y
// Rp = 1 / G
// Xp =-1 / B
//**************************************************************************************
float parallel_r(int i, const float* v) {
  return 1.0f / conductance(i, v);
}

float parallel_x(int i, const float* v) {
  return -1.0f / susceptance(i, v);
}

//**************************************************************************************
// Use w = 2 * pi * frequency
// Get Parallel L and C from B
//  C =  B / w
//  L = -1 / (w * B) = Xp / w
//**************************************************************************************
float parallel_c(int i, const float* v) {
  const float yi = susceptance(i, v);
  const float w = get_w(i);
  return yi / w;
}

float parallel_l(int i, const float* v) {
  const float xp = parallel_x(i, v);
  const float w = get_w(i);
  return xp / w;
}

float mod_y(int i, const float* v) {
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
float s21shunt_r(int i, const float* v) {
  (void)i;
  return get_s21_r(1.0f - v[0], -v[1], 0.5f * PORT_Z);
}

float s21shunt_x(int i, const float* v) {
  (void)i;
  return get_s21_x(1.0f - v[0], -v[1], 0.5f * PORT_Z);
}

float s21shunt_z(int i, const float* v) {
  (void)i;
  float l1 = get_l(v[0], v[1]);
  float l2 = get_l(1.0f - v[0], v[1]);
  return 0.5f * PORT_Z * vna_sqrtf(l1 / l2);
}

float s21series_r(int i, const float* v) {
  (void)i;
  return get_s21_r(v[0], v[1], 2.0f * PORT_Z);
}

float s21series_x(int i, const float* v) {
  (void)i;
  return get_s21_x(v[0], v[1], 2.0f * PORT_Z);
}

float s21series_z(int i, const float* v) {
  (void)i;
  float l1 = get_l(v[0], v[1]);
  float l2 = get_l(1.0f - v[0], v[1]);
  return 2.0f * PORT_Z * vna_sqrtf(l2 / l1);
}

float s21_qualityfactor(int i, const float* v) {
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
float time_of_index(int idx) {
  freq_t span = get_sweep_frequency(ST_SPAN);
  return (idx * (sweep_points - 1)) / ((float)FFT_SIZE * span);
}

float distance_of_index(int idx) {
  return velocity_factor * (SPEED_OF_LIGHT / 200.0f) * time_of_index(idx);
}
static void mark_set_index(trace_index_table_t index, uint16_t i, uint16_t x, uint16_t y,
                           MarkLineState* state) {
  chDbgAssert(i < SWEEP_POINTS_MAX, "index overflow");
  state->diff <<= 1;
  if (TRACE_X(index, i) != x || TRACE_Y(index, i) != y)
    state->diff |= 1u;
  if ((state->diff & 3u) != 0u && i > 0) {
    plot_mark_line(state->last_x, state->last_y, TRACE_X(index, i), TRACE_Y(index, i));
    plot_mark_line(TRACE_X(index, i - 1), TRACE_Y(index, i - 1), x, y);
  }
  state->last_x = TRACE_X(index, i);
  state->last_y = TRACE_Y(index, i);
  TRACE_X(index, i) = x;
  TRACE_Y(index, i) = y;
}
void trace_into_index(int t) {
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
      // For Smith chart, use proper transformation instead of cartesian coordinates
      if (trace[t].type == TRC_SMITH) {
        float real = array[i][0];
        float imag = array[i][1];
        // Calculate |Γ|^2 = real^2 + imag^2
        float mag2 = real*real + imag*imag;
        // Apply Smith chart transformation: x = cx + R * (2*real) / (1 + real^2 + imag^2)
        // y = cy - R * (2*imag) / (1 + real^2 + imag^2)
        // But since we're using a scale factor, we need to account for it properly
        float denominator = 1.0f + mag2;
        if (denominator > 0.001f) { // Avoid division by very small numbers
          x = P_CENTER_X + float2int((2.0f * real / denominator) * rscale);
          y = P_CENTER_Y - float2int((2.0f * imag / denominator) * rscale);
        } else {
          // If denominator is too small, this is near the center (matched condition)
          x = P_CENTER_X;
          y = P_CENTER_Y;
        }
        
        // Apply clipping to keep points within display bounds
        if (x < CELLOFFSETX)
          x = CELLOFFSETX;
        else if (x > CELLOFFSETX + WIDTH)
          x = CELLOFFSETX + WIDTH;
        if (y < 0)
          y = 0;
        else if (y > HEIGHT)
          y = HEIGHT;
      } else {
        // For polar charts, use the existing cartesian scale approach
        cartesian_scale(array[i], &x, &y, rscale);
      }
      mark_set_index(index, i, (uint16_t)x, (uint16_t)y, &line_state);
    }
    return;
  }
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
