/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
 * All rights reserved.
 */

#ifdef __VNA_MEASURE_MODULE__
// Use size optimization (module not need fast speed, better have smallest size)
#pragma GCC push_options
#pragma GCC optimize("Os")

#include "rf/analysis/measurement_analysis.h"

// Memory for measure cache data
// Defined in measurement_analysis.c, exposed via header
// We cast it to local types for use in View/Controller functions

// ================================================================================================
// LC Match View/Controller
// ================================================================================================
#ifdef __USE_LC_MATCHING__

static lc_match_array_t* lc_match_array = (lc_match_array_t*)measure_memory;

static void prepare_lc_match(uint8_t mode, uint8_t update_mask) {
  (void)mode;
  (void)update_mask;
  // Made calculation only one time for current sweep and frequency
  freq_t freq = get_marker_frequency(active_marker);
  if (freq == 0) // || lc_match_array->Hz == freq)
    return;

  lc_match_array->R0 = PORT_Z; // 50.0f
  lc_match_array->Hz = freq;
  // compute the possible LC matches
  lc_match_array->num_matches = lc_match_calc(markers[active_marker].index);

  // Mark to redraw area under L/C match text
  invalidate_rect(STR_MEASURE_X, STR_MEASURE_Y, STR_MEASURE_X + 3 * STR_MEASURE_WIDTH,
                  STR_MEASURE_Y + (4 + 2) * STR_MEASURE_HEIGHT);
}

//
static void lc_match_x_str(uint32_t FHz, float X, int xp, int yp) {
  if (isnan(X) || 0.0f == X || -0.0f == X)
    return;

  char type;
#if 0
  float val;
  if (X < 0.0f) {val = 1.0f / (2.0f * VNA_PI * FHz * -X); type = S_FARAD[0];}
  else          {val =    X / (2.0f * VNA_PI * FHz);      type = S_HENRY[0];}
#else
  if (X < 0.0f) {
    X = -1.0f / X;
    type = S_FARAD[0];
  } else {
    type = S_HENRY[0];
  }
  float val = X / ((2.0f * VNA_PI) * FHz);
#endif
  cell_printf(xp, yp, "%4.2F%c", val, type);
}

// Render L/C match to cell
static void draw_lc_match(int xp, int yp) {
  cell_printf(xp, yp, "L/C match for source Z0 = %0.1f" S_OHM, lc_match_array->R0);
#if 0
  yp += STR_MEASURE_HEIGHT;
  cell_printf(xp, yp, "%q" S_Hz " %0.1f %c j%0.1f" S_OHM, match_array->Hz, match_array->RL, (match_array->XL >= 0) ? '+' : '-', vna_fabsf(match_array->XL));
#endif
  yp += STR_MEASURE_HEIGHT;
  if (yp >= CELLHEIGHT)
    return;
  if (lc_match_array->num_matches < 0)
    cell_printf(xp, yp, "No LC match for this");
  else if (lc_match_array->num_matches == 0)
    cell_printf(xp, yp, "No need for LC match");
  else {
    cell_printf(xp, yp, "Src shunt");
    cell_printf(xp + STR_MEASURE_WIDTH, yp, "Series");
    cell_printf(xp + 2 * STR_MEASURE_WIDTH, yp, "Load shunt");
    for (int i = 0; i < lc_match_array->num_matches; i++) {
      yp += STR_MEASURE_HEIGHT;
      if (yp >= CELLHEIGHT)
        return;
      lc_match_x_str(lc_match_array->Hz, lc_match_array->matches[i].xps, xp, yp);
      lc_match_x_str(lc_match_array->Hz, lc_match_array->matches[i].xs, xp + STR_MEASURE_WIDTH, yp);
      lc_match_x_str(lc_match_array->Hz, lc_match_array->matches[i].xpl, xp + 2 * STR_MEASURE_WIDTH,
                     yp);
    }
  }
}
#endif // __USE_LC_MATCHING__

// ================================================================================================
// S21 / Series View/Controller
// ================================================================================================
#ifdef __S21_MEASURE__

static s21_analysis_t* s21_measure = (s21_analysis_t*)measure_memory;

static void draw_serial_result(int xp, int yp) {
  cell_printf(xp, yp, s21_measure->header);
  yp += STR_MEASURE_HEIGHT;
  if (s21_measure->freq == 0 && s21_measure->freq1 == 0) {
    cell_printf(xp, yp, "Not found");
    return;
  }
  if (s21_measure->freq) {
    cell_printf(xp, yp, "Fs=%q" S_Hz, s21_measure->freq);
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "Lm=%F" S_HENRY "  Cm=%F" S_FARAD "  Rm=%F" S_OHM,
                s21_measure->l, s21_measure->c, s21_measure->r);
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "Q=%.3f", s21_measure->q);
    //  cell_printf(xp, yp+=STR_MEASURE_HEIGHT, "tan45=%.4f", s21_measure->tan45);
    //  cell_printf(xp, yp+=STR_MEASURE_HEIGHT, "F1=%q" S_Hz " F2=%q" S_Hz, s21_measure->f1,
    //  s21_measure->f2);
  }
  if (s21_measure->freq1) {
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "Fp=%q" S_Hz "  " S_DELTA "F=%d", s21_measure->freq1,
                s21_measure->df);
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "Cp=%F" S_FARAD, s21_measure->c1);
  }
}

static void prepare_series(uint8_t type, uint8_t update_mask) {
  (void)update_mask;
  uint16_t n;
  // for detect completion
  s21_measure->freq = 0;
  s21_measure->freq1 = 0;
  switch (type) {
  case MEASURE_SHUNT_LC:
    n = 4;
    analysis_lcshunt();
    break;
  case MEASURE_SERIES_LC:
    n = 4;
    analysis_lcseries();
    break;
  case MEASURE_SERIES_XTAL:
    n = 6;
    analysis_xtalseries();
    break;
  default:
    return;
  }
  // Prepare for update
  invalidate_rect(STR_MEASURE_X, STR_MEASURE_Y, STR_MEASURE_X + 3 * STR_MEASURE_WIDTH,
                  STR_MEASURE_Y + n * STR_MEASURE_HEIGHT);
  markmap_all_markers();
}

static s21_filter_measure_t* s21_filter = (s21_filter_measure_t*)measure_memory;

static void draw_s21_pass(int xp, int yp, s21_pass* p, const char* name) {
  cell_printf(xp, yp, name);
  if (p->f[_3dB])
    cell_printf(xp, yp + STR_MEASURE_HEIGHT, "%.6F" S_Hz, p->f[_3dB]);
  if (p->f[_6dB])
    cell_printf(xp, yp + 2 * STR_MEASURE_HEIGHT, "%.6F" S_Hz, p->f[_6dB]);
  yp += 3 * STR_MEASURE_HEIGHT;
  if (p->decade) {
    cell_printf(xp, yp, "%F" S_dB "/dec", p->decade);
    cell_printf(xp, yp + STR_MEASURE_HEIGHT, "%F" S_dB "/oct", p->octave);
  }
}

#define S21_MEASURE_FILTER_THRESHOLD -50.0f
static void draw_filter_result(int xp, int yp) {
  cell_printf(xp, yp, "S21 FILTER");
  if (s21_filter->vmax < S21_MEASURE_FILTER_THRESHOLD)
    return;
  yp += STR_MEASURE_HEIGHT;
  // f: ___.___MHz (xxxdB)
  // Bw(-3dB): ___.___MHz
  // Bw(-6dB): ___.___MHz
  // Q: xxx
  if (s21_filter->f_center) {
    cell_printf(xp, yp, "f: %.6F" S_Hz " (%F" S_dB ")", s21_filter->f_center, s21_filter->vmax);
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "Bw (-%d" S_dB "): %.6F" S_Hz, 3, s21_filter->bw_3dB);
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "Bw (-%d" S_dB "): %.6F" S_Hz, 6, s21_filter->bw_6dB);
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "Q: %F", s21_filter->q);
  } else {
    cell_printf(xp, yp, "f: %.6F" S_Hz " (%F" S_dB ")", s21_filter->fmax, s21_filter->vmax);
  }
  // Lo/Hi pass data show
  const int width0 = 3 * STR_MEASURE_WIDTH * 2 / 10; // 1 column width 20%
  const int width1 = 3 * STR_MEASURE_WIDTH * 4 / 10; // 2 and 3 column 40%
  //  20%  |   40%     |    40%
  //        Low-side    High-side
  // f(-3)  ___.___MHz  ___.___MHz
  // f(-6)  ___.___MHz  ___.___MHz
  // Roll:  ___dB/dec   ___dB/oct
  //        ___dB/dec   ___dB/oct
  if (s21_filter->lo_pass.f[_3dB] || s21_filter->hi_pass.f[_3dB]) {
    yp += STR_MEASURE_HEIGHT;
    cell_printf(xp, yp + 1 * STR_MEASURE_HEIGHT, "f(-%d):", 3);
    cell_printf(xp, yp + 2 * STR_MEASURE_HEIGHT, "f(-%d):", 6);
    cell_printf(xp, yp + 3 * STR_MEASURE_HEIGHT, "Roll:");
    xp += width0;
    if (s21_filter->hi_pass.f[_3dB]) {
      draw_s21_pass(xp, yp, &s21_filter->hi_pass, s21_filter->f_center ? "Low-side" : "High-pass");
      xp += width1;
    }
    if (s21_filter->lo_pass.f[_3dB]) {
      draw_s21_pass(xp, yp, &s21_filter->lo_pass, s21_filter->f_center ? "High-side" : "Low-pass");
    }
  }
}

static void prepare_filter(uint8_t type, uint8_t update_mask) {
  (void)type;
  (void)update_mask;
  uint16_t xp = 0;
  s21_filter->vmax = search_peak_value(&xp, s21logmag, MEASURE_SEARCH_MAX); // Maximum search
  // If maximum < 50dB, no filter detected
  if (s21_filter->vmax >= S21_MEASURE_FILTER_THRESHOLD) {
    set_marker_index(0, xp);              // Put marker on maximum value point
    s21_filter->fmax = get_frequency(xp); // Get maximum value frequency
    find_filter_pass(
        s21_filter->vmax, &s21_filter->hi_pass, xp,
        MEASURE_SEARCH_LEFT); // Search High-pass filter data (or Low side for bandpass)
    find_filter_pass(
        s21_filter->vmax, &s21_filter->lo_pass, xp,
        MEASURE_SEARCH_RIGHT); // Search Low-pass filter data (or High side for bandpass)
    // Calculate Band-pass filter data
    s21_filter->f_center =
        s21_filter->lo_pass.f[_3dB] *
        s21_filter->hi_pass.f[_3dB]; // Center frequency (if 0, one or both points not found)
    if (s21_filter->f_center) {
      s21_filter->bw_3dB = s21_filter->lo_pass.f[_3dB] - s21_filter->hi_pass.f[_3dB];
      s21_filter->bw_6dB = s21_filter->lo_pass.f[_6dB] - s21_filter->hi_pass.f[_6dB];
      s21_filter->f_center = vna_sqrtf(s21_filter->f_center);
      s21_filter->q = s21_filter->f_center / s21_filter->bw_3dB;
    }
  }
  // Prepare for update
  invalidate_rect(STR_MEASURE_X, STR_MEASURE_Y, STR_MEASURE_X + 3 * STR_MEASURE_WIDTH,
                  STR_MEASURE_Y + 10 * STR_MEASURE_HEIGHT);
}
#endif // __S21_MEASURE__

// ================================================================================================
// S11 Cable View/Controller
// ================================================================================================
#ifdef __S11_CABLE_MEASURE__

static s11_cable_measure_t* s11_cable = (s11_cable_measure_t*)measure_memory;
float real_cable_len = 0.0f;

static void draw_s11_cable(int xp, int yp) {
  cell_printf(xp, yp, "S11 CABLE");
  if (s11_cable->R) {
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "Z0 = %F" S_OHM, s11_cable->R);
    //    cell_printf(xp, yp+=FONT_STR_HEIGHT, "C0 = %F" S_FARAD "/" S_METRE, s11_cable->C0);
  }
  if (s11_cable->vf)
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "VF=%.2f%% (Length = %F" S_METRE ")", s11_cable->vf,
                real_cable_len);
  else if (s11_cable->len)
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "Length = %F" S_METRE " (VF=%d%%)", s11_cable->len,
                velocity_factor);
  // cell_printf(xp, yp+=STR_MEASURE_HEIGHT, "Loss = %F" S_dB " (%.4F" S_Hz ")", s11_cable->loss,
  // s11_cable->freq);
  cell_printf(xp, yp += STR_MEASURE_HEIGHT, "Loss = %F" S_dB " (%.4F" S_Hz ")", s11_cable->mloss,
              s11_cable->freq);
  if (s11_cable->len) {
    float l = s11_cable->len;
    cell_printf(xp, yp += STR_MEASURE_HEIGHT,
                "Att (" S_dB "/100" S_METRE "): %F" S_dB " (%.4F" S_Hz ")",
                s11_cable->mloss * 100.0f / l, s11_cable->freq);
    //    cell_printf(xp, yp+=STR_MEASURE_HEIGHT, "DC: %.6F, K1: %.6F, K2: %.6F", s11_cable->a / l,
    //    s11_cable->b / l, s11_cable->c / l);
  }
}

static void prepare_s11_cable(uint8_t type, uint8_t update_mask) {
  (void)type;
  freq_t f1;
  if (update_mask & MEASURE_UPD_SWEEP) {
    s11_cable->R = 0.0f;
    s11_cable->len = 0.0f;
    s11_cable->vf = 0.0f;
    uint16_t x = 0;
    f1 = measure_search_value(&x, 0, s11imag, MEASURE_SEARCH_RIGHT, MARKER_INVALID);
    if (f1) {
      float electric_length = (SPEED_OF_LIGHT / 400.0f) / f1;
      if (real_cable_len != 0.0f) {
        s11_cable->len = real_cable_len;
        s11_cable->vf = real_cable_len / electric_length;
      } else
        s11_cable->len = velocity_factor * electric_length;
      float data[2];
      if (measure_get_value(0, f1 / 2, data)) {
        s11_cable->R = vna_fabsf(reactance(0, data));
        //        s11_cable->C0 = velocity_factor / (100.0f * SPEED_OF_LIGHT * s11_cable->R);
      }
    }
    parabolic_regression(sweep_points, s11index, s11loss, &s11_cable->a);
  }
  if ((update_mask & MEASURE_UPD_ALL) && active_marker != MARKER_INVALID) {
    int idx = markers[active_marker].index;
    //  s11_cable->loss  = s11loss(idx);
    s11_cable->freq = (float)get_frequency(idx);
    float f = s11_cable->freq * 1e-9f;
    s11_cable->mloss = s11_cable->a + s11_cable->b * vna_sqrtf(f) + s11_cable->c * f;
  }
  // Prepare for update
  invalidate_rect(STR_MEASURE_X, STR_MEASURE_Y, STR_MEASURE_X + 3 * STR_MEASURE_WIDTH,
                  STR_MEASURE_Y + 6 * STR_MEASURE_HEIGHT);
}

#endif // __S11_CABLE_MEASURE__

// ================================================================================================
// S11 Resonance View/Controller
// ================================================================================================
#ifdef __S11_RESONANCE_MEASURE__

static s11_resonance_measure_t* s11_resonance = (s11_resonance_measure_t*)measure_memory;

static void draw_s11_resonance(int xp, int yp) {
  cell_printf(xp, yp, "S11 RESONANCE");
  if (s11_resonance->count == 0) {
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "Not found");
    return;
  }
  for (int i = 0; i < s11_resonance->count; i++)
    cell_printf(xp, yp += STR_MEASURE_HEIGHT, "%q" S_Hz ", %F%+jF" S_OHM, s11_resonance->data[i].f,
                s11_resonance->data[i].r, s11_resonance->data[i].x);
}

static void prepare_s11_resonance(uint8_t type, uint8_t update_mask) {
  (void)type;
  if (update_mask & MEASURE_UPD_SWEEP) {
    int i;
    freq_t f;
    uint16_t x = 0;
    // Search resonances (X == 0)
    for (i = 0; i < MEASURE_RESONANCE_COUNT && i < MARKERS_MAX;) {
      f = measure_search_value(&x, 0.0f, s11_resonance_value, MEASURE_SEARCH_RIGHT, MARKER_INVALID);
      if (f == 0)
        break;
      if (add_resonance_value(i, x, f))
        i++;
      x++;
    }
    if (i == 0) { // Search minimum position, if resonances not found
      x = 0;
      search_peak_value(&x, s11_resonance_min, MEASURE_SEARCH_MIN);
      if (x && add_resonance_value(0, x, get_frequency(x)))
        i = 1;
    }
    s11_resonance->count = i;
  }
  // Prepare for update
  invalidate_rect(STR_MEASURE_X, STR_MEASURE_Y, STR_MEASURE_X + 3 * STR_MEASURE_WIDTH,
                  STR_MEASURE_Y + (MEASURE_RESONANCE_COUNT + 1) * STR_MEASURE_HEIGHT);
}
#endif //__S11_RESONANCE_MEASURE__
#pragma GCC pop_options
#endif // __VNA_MEASURE_MODULE__
