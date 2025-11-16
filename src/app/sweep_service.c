/*
 * Measurement sweep orchestration for NanoVNA-X.
 *
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
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

#include "app/sweep_service.h"
#include "app/shell.h"

#include "hal.h"
#include "si5351.h"

#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#ifdef __VNA_Z_RENORMALIZATION__
#include "../modules/vna_renorm.c"
#endif

/*
 * DMA/I2S capture state
 */
static systime_t ready_time = 0;
static volatile uint16_t wait_count = 0;
static alignas(4) audio_sample_t rx_buffer[AUDIO_BUFFER_LEN * 2];

#if ENABLED_DUMP_COMMAND
static audio_sample_t* dump_buffer = NULL;
static volatile int16_t dump_len = 0;
static int16_t dump_selection = 0;
#endif

/*
 * Sweep execution state shared across the firmware.
 */
static volatile bool sweep_in_progress = false;
static volatile bool sweep_copy_in_progress = false;
static volatile uint32_t sweep_generation = 0;
static uint16_t p_sweep = 0;
static uint8_t sweep_state_flags = 0;
static uint16_t sweep_bar_drawn_pixels = 0;
static uint8_t sweep_bar_pending = 0;
static bool sweep_workspace_locked = false;

static uint8_t smooth_factor = 0;
static void (*volatile sample_func)(float* gamma) = calculate_gamma;

typedef struct {
  float samples[4];
  float calibration[CAL_TYPE_COUNT][2];
} sweep_measurement_buffers_t;

static sweep_measurement_buffers_t sweep_buffers;

void sweep_service_set_sample_function(void (*func)(float*)) {
  if (func == NULL) {
    func = calculate_gamma;
  }
  osalSysLock();
  sample_func = func;
  osalSysUnlock();
}

static inline bool sweep_ui_input_pending(void) {
  return operation_request_pending(OP_TOUCH | OP_LEVER | OP_CONSOLE);
}

#ifdef __USE_FREQ_TABLE__
static freq_t frequencies[SWEEP_POINTS_MAX];
#else
static freq_t _f_start;
static freq_t _f_delta;
static freq_t _f_error;
static uint16_t _f_points;
#endif

static float arifmetic_mean(float v0, float v1, float v2) {
  return (v0 + 2.0f * v1 + v2) * 0.25f;
}

static float geometry_mean(float v0, float v1, float v2) {
  float v = vna_cbrtf(vna_fabsf(v0 * v1 * v2));
  if ((v0 + v1 + v2) < 0.0f) {
    v = -v;
  }
  return v;
}

#define SWEEP_STATE_PROGRESS_ACTIVE 0x01u
#define SWEEP_STATE_LED_ACTIVE 0x02u

static inline void sweep_reset_progress(void) {
  p_sweep = 0;
}

static inline bool sweep_progress_enabled(void) {
  return (sweep_state_flags & SWEEP_STATE_PROGRESS_ACTIVE) != 0U;
}

static void sweep_led_begin(void) {
  if ((sweep_state_flags & SWEEP_STATE_LED_ACTIVE) == 0U) {
    palClearPad(GPIOC, GPIOC_LED);
    sweep_state_flags |= SWEEP_STATE_LED_ACTIVE;
  }
}

static void sweep_led_end(void) {
  if ((sweep_state_flags & SWEEP_STATE_LED_ACTIVE) != 0U) {
    palSetPad(GPIOC, GPIOC_LED);
    sweep_state_flags &= (uint8_t)~SWEEP_STATE_LED_ACTIVE;
  }
}

static void sweep_progress_begin(bool enabled) {
  sweep_bar_drawn_pixels = 0;
  sweep_bar_pending = 0;
  sweep_state_flags &= (uint8_t)~SWEEP_STATE_PROGRESS_ACTIVE;
  if (!enabled) {
    return;
  }
  sweep_state_flags |= SWEEP_STATE_PROGRESS_ACTIVE;
  lcd_set_background(LCD_SWEEP_LINE_COLOR);
}

static void sweep_progress_update(uint16_t pixels) {
  if (!sweep_progress_enabled() || pixels <= sweep_bar_drawn_pixels) {
    return;
  }
  if (pixels > WIDTH) {
    pixels = WIDTH;
  }
  uint16_t delta = pixels - sweep_bar_drawn_pixels - sweep_bar_pending;
  uint16_t draw = sweep_bar_pending + delta;
  if (draw >= 2U || pixels >= WIDTH) {
    lcd_fill(OFFSETX + CELLOFFSETX + sweep_bar_drawn_pixels, OFFSETY, draw, 1);
    sweep_bar_drawn_pixels += draw;
    sweep_bar_pending = 0;
  } else {
    sweep_bar_pending = (uint8_t)draw;
  }
}

static void sweep_progress_end(void) {
  if (!sweep_progress_enabled()) {
    return;
  }
  if (sweep_bar_pending > 0U) {
    lcd_fill(OFFSETX + CELLOFFSETX + sweep_bar_drawn_pixels, OFFSETY, sweep_bar_pending, 1);
    sweep_bar_drawn_pixels += sweep_bar_pending;
  }
  sweep_bar_pending = 0;
  lcd_set_background(LCD_GRID_COLOR);
  if (sweep_bar_drawn_pixels > 0U) {
    lcd_fill(OFFSETX + CELLOFFSETX, OFFSETY, sweep_bar_drawn_pixels, 1);
  }
  sweep_bar_drawn_pixels = 0;
  sweep_state_flags &= (uint8_t)~SWEEP_STATE_PROGRESS_ACTIVE;
}

static inline uint16_t sweep_points_budget(bool break_on_operation) {
  if (!break_on_operation) {
    return UINT16_MAX;
  }
  if (sweep_ui_input_pending()) {
    return 1U;
  }
  if (config._bandwidth <= BANDWIDTH_100) {
    return 4U;
  }
  if (config._bandwidth <= BANDWIDTH_1000) {
    return 6U;
  }
  return 8U;
}

static inline void sweep_prepare_led_and_progress(bool show_progress) {
  sweep_led_begin();
  sweep_progress_begin(show_progress);
}

static void apply_edelay(float w, float data[2]) {
  float s, c;
  float real = data[0];
  float imag = data[1];
  vna_sincosf(w, &s, &c);
  data[0] = real * c - imag * s;
  data[1] = imag * c + real * s;
}

static void apply_offset(float data[2], float offset) {
  data[0] *= offset;
  data[1] *= offset;
}

#if ENABLED_DUMP_COMMAND
static void duplicate_buffer_to_dump(audio_sample_t* p, size_t n) {
  p += dump_selection;
  while (n > 0U) {
    if (dump_len == 0) {
      return;
    }
    dump_len--;
    *dump_buffer++ = *p;
    p += 2;
    n -= 2;
  }
}
#endif

void i2s_lld_serve_rx_interrupt(uint32_t flags) {
  uint16_t wait = wait_count;
  if (wait == 0U || chVTGetSystemTimeX() < ready_time) {
    return;
  }
  audio_sample_t* p = (flags & STM32_DMA_ISR_TCIF) ? rx_buffer + AUDIO_BUFFER_LEN : rx_buffer;
  if (wait >= config._bandwidth + 2U) {
    reset_dsp_accumerator();
  } else {
    dsp_process(p, AUDIO_BUFFER_LEN);
  }
#if ENABLED_DUMP_COMMAND
  duplicate_buffer_to_dump(p, AUDIO_BUFFER_LEN);
#endif
  --wait_count;
}

void sweep_service_init(void) {
  sweep_in_progress = false;
  sweep_copy_in_progress = false;
  sweep_generation = 0;
  sweep_reset_progress();
  sweep_state_flags = 0;
  sweep_bar_drawn_pixels = 0;
  sweep_bar_pending = 0;
  smooth_factor = 0;
  sample_func = calculate_gamma;
#if ENABLED_DUMP_COMMAND
  dump_buffer = NULL;
  dump_len = 0;
  dump_selection = 0;
#endif
}

void sweep_service_reset_progress(void) {
  sweep_reset_progress();
}

void sweep_service_wait_for_copy_release(void) {
  while (true) {
    osalSysLock();
    bool busy = sweep_copy_in_progress;
    osalSysUnlock();
    if (!busy) {
      break;
    }
    chThdYield();
  }
}

void sweep_service_begin_measurement(void) {
  osalSysLock();
  sweep_in_progress = true;
  osalSysUnlock();
}

void sweep_service_end_measurement(void) {
  osalSysLock();
  sweep_in_progress = false;
  osalSysUnlock();
}

uint32_t sweep_service_increment_generation(void) {
  osalSysLock();
  uint32_t generation = ++sweep_generation;
  osalSysUnlock();
  return generation;
}

uint32_t sweep_service_current_generation(void) {
  osalSysLock();
  uint32_t generation = sweep_generation;
  osalSysUnlock();
  return generation;
}

void sweep_service_wait_for_generation(void) {
  systime_t start_time = chVTGetSystemTimeX();
  systime_t timeout = MS2ST(1000); // 1 second timeout to prevent infinite wait
  
  while (sweep_service_current_generation() == 0U) {
    if (chVTGetSystemTimeX() - start_time >= timeout) {
      // Timeout occurred, break to prevent hanging
      break;
    }
    chThdSleepMilliseconds(1);
  }
}

bool sweep_service_snapshot_acquire(uint8_t channel, sweep_service_snapshot_t* snapshot) {
  if (snapshot == NULL || channel >= 2U) {
    return false;
  }
  systime_t start_time = chVTGetSystemTimeX();
  systime_t timeout = MS2ST(2000); // 2 second timeout to prevent infinite wait
  
  while (true) {
    osalSysLock();
    bool busy = sweep_in_progress || sweep_copy_in_progress;
    if (!busy) {
      sweep_copy_in_progress = true;
      snapshot->generation = sweep_generation;
      snapshot->points = sweep_points;
      snapshot->data = measured[channel];
      osalSysUnlock();
      return true;
    }
    osalSysUnlock();
    
    // Check for timeout
    if (chVTGetSystemTimeX() - start_time >= timeout) {
      return false; // Timeout occurred, unable to acquire snapshot
    }
    
    chThdSleepMilliseconds(1);
  }
}

bool sweep_service_snapshot_release(const sweep_service_snapshot_t* snapshot) {
  bool stable = false;
  osalSysLock();
  if (snapshot != NULL) {
    stable = (snapshot->generation == sweep_generation);
  }
  sweep_copy_in_progress = false;
  osalSysUnlock();
  return stable;
}

bool sweep_service_workspace_acquire(sweep_workspace_t* workspace) {
  if (workspace == NULL) {
    return false;
  }
  bool acquired = false;
  osalSysLock();
  if (!sweep_workspace_locked && !sweep_in_progress && !sweep_copy_in_progress) {
    sweep_workspace_locked = true;
    workspace->buffer = (uint8_t*)measured[1];
    workspace->size = sizeof(measured[1]);
    acquired = true;
  } else {
    workspace->buffer = NULL;
    workspace->size = 0;
  }
  osalSysUnlock();
  return acquired;
}

void sweep_service_workspace_release(void) {
  osalSysLock();
  sweep_workspace_locked = false;
  osalSysUnlock();
}

void sweep_service_start_capture(systime_t delay_ticks) {
  ready_time = chVTGetSystemTimeX() + delay_ticks;
  wait_count = config._bandwidth + 2U;
}

bool sweep_service_wait_for_capture(void) {
  systime_t start_time = chVTGetSystemTimeX();
  systime_t timeout = MS2ST(500); // 500ms timeout - adjust as needed
  while (wait_count != 0U) {
    systime_t current_time = chVTGetSystemTimeX();
    if (current_time - start_time >= timeout) {
      // Timeout occurred - break to prevent hanging
      // This can happen if I2S interrupts don't fire properly (e.g. USB not connected)
      wait_count = 0;
      reset_dsp_accumerator();
      return false;
    }
    __WFI();
  }
  return true;
}

const audio_sample_t* sweep_service_rx_buffer(void) {
  return rx_buffer;
}

#if ENABLED_DUMP_COMMAND
void sweep_service_prepare_dump(audio_sample_t* buffer, size_t count, int selection) {
  chDbgAssert(((uintptr_t)buffer & 0x3U) == 0U, "audio sample buffer must be 4-byte aligned");
  dump_buffer = buffer;
  dump_len = (int16_t)count;
  dump_selection = (int16_t)selection;
}

bool sweep_service_dump_ready(void) {
  return dump_len <= 0;
}
#endif

uint16_t app_measurement_get_sweep_mask(void) {
  uint16_t ch_mask = 0;
#if 0
  for (int t = 0; t < TRACES_MAX; t++) {
    if (!trace[t].enabled) {
      continue;
    }
    if ((trace[t].channel & 1) == 0) {
      ch_mask |= SWEEP_CH0_MEASURE;
    } else {
      ch_mask |= SWEEP_CH1_MEASURE;
    }
  }
#else
  ch_mask |= SWEEP_CH0_MEASURE | SWEEP_CH1_MEASURE;
#endif
#ifdef __VNA_MEASURE_MODULE__
  ch_mask |= plot_get_measure_channels();
#endif
#ifdef __VNA_Z_RENORMALIZATION__
  if (current_props._portz != cal_load_r) {
    ch_mask |= SWEEP_USE_RENORMALIZATION;
  }
#endif
  if (cal_status & CALSTAT_APPLY) {
    ch_mask |= SWEEP_APPLY_CALIBRATION;
  }
  if (cal_status & CALSTAT_INTERPOLATED) {
    ch_mask |= SWEEP_USE_INTERPOLATION;
  }
  if (electrical_delayS11) {
    ch_mask |= SWEEP_APPLY_EDELAY_S11;
  }
  if (electrical_delayS21) {
    ch_mask |= SWEEP_APPLY_EDELAY_S21;
  }
  if (s21_offset) {
    ch_mask |= SWEEP_APPLY_S21_OFFSET;
  }
  return ch_mask;
}

static void apply_ch0_error_term(float data[4], float c_data[CAL_TYPE_COUNT][2]) {
  float re = data[0];
  float im = data[1];
  float c_re = c_data[ETERM_ED][0];
  float c_im = c_data[ETERM_ED][1];
  float esr = c_data[ETERM_ES][0];
  float esi = c_data[ETERM_ES][1];
  float err = c_data[ETERM_ER][0];
  float eri = c_data[ETERM_ER][1];
  float numerator_r = re - c_re;
  float numerator_i = im - c_im;
  float denominator_r = 1.0f - esr * numerator_r + esi * numerator_i;
  float denominator_i = 0.0f - esr * numerator_i - esi * numerator_r;
  float inv = denominator_r * denominator_r + denominator_i * denominator_i;
  if (inv != 0.0f) {
    inv = 1.0f / inv;
  }
  float denom_conj_r = denominator_r * inv;
  float denom_conj_i = -denominator_i * inv;
  data[0] = (numerator_r * denom_conj_r - numerator_i * denom_conj_i) - err;
  data[1] = (numerator_i * denom_conj_r + numerator_r * denom_conj_i) - eri;
}

static void apply_ch1_error_term(float data[4], float c_data[CAL_TYPE_COUNT][2]) {
  float s21mr = data[2] - c_data[ETERM_EX][0];
  float s21mi = data[3] - c_data[ETERM_EX][1];
  float esr = 1 - (c_data[ETERM_ES][0] * data[0] - c_data[ETERM_ES][1] * data[1]);
  float esi = -(c_data[ETERM_ES][1] * data[0] + c_data[ETERM_ES][0] * data[1]);
  float etr = esr * c_data[ETERM_ET][0] - esi * c_data[ETERM_ET][1];
  float eti = esr * c_data[ETERM_ET][1] + esi * c_data[ETERM_ET][0];
  float denom = etr * etr + eti * eti;
  if (denom != 0.0f) {
    denom = 1.0f / denom;
  }
  float et_conj_r = etr * denom;
  float et_conj_i = -eti * denom;
  float s21ar = s21mr * et_conj_r - s21mi * et_conj_i;
  float s21ai = s21mi * et_conj_r + s21mr * et_conj_i;
  float isoln_r = c_data[CAL_ISOLN][0];
  float isoln_i = c_data[CAL_ISOLN][1];
  data[2] = s21ar + isoln_r;
  data[3] = s21ai + isoln_i;
}

static void cal_interpolate(int idx, freq_t f, float data[CAL_TYPE_COUNT][2]) {
  if (idx < 0) {
    freq_t start = cal_frequency0;
    freq_t stop = cal_frequency1;
    uint16_t points = cal_sweep_points;
    if (points <= 1U || stop == start) {
      idx = 0;
    } else {
      freq_t span = stop - start;
      int32_t k = (int32_t)((uint64_t)(f - start) * (points - 1U) / span);
      if (k < 0) {
        k = 0;
      } else if (k >= (int32_t)(points - 1U)) {
        k = (int32_t)(points - 1U);
      }
      idx = k;
    }
  }
  for (uint16_t eterm = 0; eterm < CAL_TYPE_COUNT; eterm++) {
    data[eterm][0] = cal_data[eterm][idx][0];
    data[eterm][1] = cal_data[eterm][idx][1];
  }
}

bool app_measurement_sweep(bool break_on_operation, uint16_t mask) {
  static const uint16_t channel_measure_mask[2] = {SWEEP_CH0_MEASURE, SWEEP_CH1_MEASURE};
  static const uint16_t channel_edelay_mask[2] = {SWEEP_APPLY_EDELAY_S11, SWEEP_APPLY_EDELAY_S21};
  static const uint8_t channel_sample_offset[2] = {0U, 2U};
  if (p_sweep >= sweep_points || !break_on_operation) {
    sweep_reset_progress();
    sweep_progress_end();
    sweep_led_end();
  }
  if (break_on_operation && mask == 0U) {
    sweep_progress_end();
    sweep_led_end();
    return false;
  }
  sweep_measurement_buffers_t* buffers = &sweep_buffers;
  float* data = buffers->samples;
  float (*calibration_data)[2] = buffers->calibration;
  bool completed = false;
  int delay = 0;
  int st_delay = DELAY_SWEEP_START;
  int interpolation_idx = p_sweep;
  bool show_progress = config._bandwidth >= BANDWIDTH_100;
  uint16_t batch_budget = sweep_points_budget(break_on_operation);
  uint16_t processed = 0;
  const bool apply_offset_s21 = (mask & SWEEP_APPLY_S21_OFFSET) != 0U;
  const bool apply_calibration = (mask & SWEEP_APPLY_CALIBRATION) != 0U;
  const bool measurement_required = (mask & (SWEEP_CH0_MEASURE | SWEEP_CH1_MEASURE)) != 0U;
#ifdef __VNA_Z_RENORMALIZATION__
  const bool apply_renorm = (mask & SWEEP_USE_RENORMALIZATION) != 0U;
#endif
  const bool apply_s21_offset = apply_offset_s21 && (mask & SWEEP_CH1_MEASURE);
  const float offset_scale =
      apply_offset_s21 ? vna_expf(s21_offset * (logf(10.0f) / 20.0f)) : 1.0f;
  void (*sample_cb)(float*) = sample_func;

  if (p_sweep == 0U) {
    sweep_prepare_led_and_progress(show_progress);
  }

  for (; p_sweep < sweep_points; p_sweep++) {
    shell_service_pending_commands();
    if (processed >= batch_budget) {
      break;
    }
    if (break_on_operation && sweep_ui_input_pending()) {
      break;
    }
    freq_t frequency = get_frequency(p_sweep);
    uint8_t extra_cycles = 0U;
    if (measurement_required) {
      delay = app_measurement_set_frequency(frequency);
      interpolation_idx = (mask & SWEEP_USE_INTERPOLATION) ? -1 : (int)p_sweep;
      extra_cycles = si5351_take_settling_cycles();
    }
    uint8_t total_cycles = extra_cycles + 1U;
    for (uint8_t cycle = 0; cycle < total_cycles; cycle++) {
      bool final_cycle = (cycle == total_cycles - 1U);
      int cycle_delay = delay;
      int cycle_st_delay = (cycle == 0U) ? st_delay : 0;
      bool calibration_ready = false;
      for (uint8_t channel = 0; channel < 2; channel++) {
        if ((mask & channel_measure_mask[channel]) == 0U) {
          continue;
        }
        tlv320aic3204_select(channel);
        sweep_service_start_capture(cycle_delay + cycle_st_delay);
        cycle_delay = DELAY_CHANNEL_CHANGE;
        if (final_cycle && apply_calibration && (!calibration_ready || channel == 0U)) {
          cal_interpolate(interpolation_idx, frequency, calibration_data);
          calibration_ready = true;
        }
        if (!sweep_service_wait_for_capture()) {
          goto capture_failure;
        }
        float* sample_store = &data[channel_sample_offset[channel]];
        sample_cb(sample_store);
        if (final_cycle && apply_calibration) {
          if (channel == 0U) {
            apply_ch0_error_term(sample_store, calibration_data);
          } else {
            apply_ch1_error_term(sample_store, calibration_data);
          }
        }
        if (mask & channel_edelay_mask[channel]) {
          const float edelay = current_props._electrical_delay[channel] * frequency;
          apply_edelay(edelay, sample_store);
        }
        if (channel == 1U && apply_s21_offset) {
          apply_offset(sample_store, offset_scale);
        }
        measured[channel][p_sweep][0] = sample_store[0];
        measured[channel][p_sweep][1] = sample_store[1];
      }
    }
#ifdef __VNA_Z_RENORMALIZATION__
    if (apply_renorm) {
      apply_renormalization(data, mask);
    }
#endif
    if (show_progress && sweep_points > 1U) {
      uint16_t current_bar = (uint16_t)(((uint32_t)p_sweep * WIDTH) / (sweep_points - 1U));
      sweep_progress_update(current_bar);
    }
    processed++;
  }
  completed = (p_sweep == sweep_points);
  if (completed) {
    sweep_progress_end();
    sweep_led_end();
  }
  return completed;

capture_failure:
  sweep_reset_progress();
  sweep_progress_end();
  sweep_led_end();
  return false;
}

void measurement_data_smooth(uint16_t ch_mask) {
  if (smooth_factor == 0U || sweep_points <= 2U) {
    return;
  }
  float (*smooth_func)(float, float, float) =
      VNA_MODE(VNA_MODE_SMOOTH) ? arifmetic_mean : geometry_mean;
  const uint32_t max_passes = sweep_points > 2U ? (uint32_t)(sweep_points - 2U) : 0U;
  uint32_t passes = 1U << (smooth_factor - 1U);
  if (passes == 0U) {
    passes = 1U;
  }
  if (passes > max_passes) {
    passes = max_passes;
  }
  if (passes == 0U) {
    return;
  }
  const uint32_t limit = (uint32_t)(sweep_points - 1U);
  for (uint8_t ch = 0; ch < 2U; ch++, ch_mask >>= 1) {
    if ((ch_mask & 1U) == 0U) {
      continue;
    }
    float* samples = measured[ch][0];
    for (uint32_t pass = 0; pass < passes; pass++) {
      float prev_real = samples[0];
      float prev_imag = samples[1];
      uint32_t index = 1U;
      float* current = samples + 2;
      while (index < limit) {
        float next_real = current[2];
        float next_imag = current[3];
        float current_real = current[0];
        float current_imag = current[1];
        current[0] = smooth_func(prev_real, current_real, next_real);
        current[1] = smooth_func(prev_imag, current_imag, next_imag);
        prev_real = current_real;
        prev_imag = current_imag;
        current += 2;
        ++index;
      }
    }
  }
}

void set_smooth_factor(uint8_t factor) {
  if (factor > 8U) {
    factor = 8U;
  }
  smooth_factor = factor;
  request_to_redraw(REDRAW_CAL_STATUS);
}

uint8_t get_smooth_factor(void) {
  return smooth_factor;
}

int app_measurement_set_frequency(freq_t freq) {
  return si5351_set_frequency(freq, current_props._power);
}

#ifdef __USE_FREQ_TABLE__
void app_measurement_set_frequencies(freq_t start, freq_t stop, uint16_t points) {
  freq_t step = points - 1U;
  freq_t span = stop - start;
  freq_t delta = span / step;
  freq_t error = span % step;
  freq_t f = start;
  freq_t df = step >> 1;
  uint32_t i = 0;
  for (; i <= step; i++, f += delta) {
    frequencies[i] = f;
    if ((df += error) >= step) {
      f++;
      df -= step;
    }
  }
  for (; i < SWEEP_POINTS_MAX; i++) {
    frequencies[i] = 0;
  }
}

freq_t get_frequency(uint16_t idx) {
  return frequencies[idx];
}

freq_t get_frequency_step(void) {
  if (sweep_points <= 1U) {
    return 0;
  }
  return frequencies[1] - frequencies[0];
}
#else
void app_measurement_set_frequencies(freq_t start, freq_t stop, uint16_t points) {
  freq_t span = stop - start;
  _f_start = start;
  _f_points = points - 1U;
  _f_delta = span / _f_points;
  _f_error = span % _f_points;
}

freq_t get_frequency(uint16_t idx) {
  return _f_start + _f_delta * idx + (_f_points / 2U + _f_error * idx) / _f_points;
}

freq_t get_frequency_step(void) {
  return _f_delta;
}
#endif

float bessel_i0_ext(float z) {
#define BESSEL_SIZE 12
  int i = BESSEL_SIZE - 1;
  static const float besseli0_k[BESSEL_SIZE - 1] = {
      2.5000000000000000000000000000000e-01f, 2.7777777777777777777777777777778e-02f,
      1.7361111111111111111111111111111e-03f, 6.9444444444444444444444444444444e-05f,
      1.9290123456790123456790123456790e-06f, 3.9367598891408415217939027462837e-08f,
      6.1511873267825648778029730410683e-10f, 7.5940584281266233059295963469979e-12f,
      7.5940584281266233059295963469979e-14f, 6.2760813455591928148178482206594e-16f,
      4.3583898233049950102901723754579e-18f};
  float term = z;
  float ret = 1.0f + z;
  do {
    term *= z;
    ret += term * besseli0_k[BESSEL_SIZE - 1 - i];
  } while (--i);
  return ret;
}

static float kaiser_window_ext(uint32_t k, uint32_t n, uint16_t beta) {
  if (beta == 0U) {
    return 1.0f;
  }
  n = n - 1U;
  k = k * (n - k) * beta * beta;
  n = n * n;
  return bessel_i0_ext((float)k / n);
}

void app_measurement_transform_domain(uint16_t ch_mask) {
  uint16_t offset = 0;
  uint8_t is_lowpass = FALSE;
  switch (domain_func) {
  case TD_FUNC_LOWPASS_IMPULSE:
  case TD_FUNC_LOWPASS_STEP:
    is_lowpass = TRUE;
    offset = sweep_points;
    break;
  default:
    break;
  }
  uint16_t window_size = sweep_points + offset;
  uint16_t beta = 0;
  switch (domain_window) {
  case TD_WINDOW_NORMAL:
    beta = 6;
    break;
  case TD_WINDOW_MAXIMUM:
    beta = 13;
    break;
  default:
    beta = 0;
    break;
  }
  static float window_scale = 0.0f;
  static uint16_t td_cache = 0;
  uint16_t td_check = (props_mode & (TD_WINDOW | TD_FUNC)) | (sweep_points << 5);
  if (td_cache != td_check) {
    td_cache = td_check;
    if (domain_func == TD_FUNC_LOWPASS_STEP) {
      window_scale = FFT_SIZE * bessel_i0_ext(beta * beta / 4.0f);
    } else {
      window_scale = 0.0f;
      for (int i = 0; i < sweep_points; i++) {
        window_scale += kaiser_window_ext(i + offset, window_size, beta);
      }
      if (domain_func == TD_FUNC_LOWPASS_IMPULSE) {
        window_scale *= 2.0f;
      }
    }
    window_scale = 1.0f / window_scale;
#ifdef USE_FFT_WINDOW_BUFFER
    static float kaiser_data[FFT_SIZE];
    for (int i = 0; i < sweep_points; i++) {
      kaiser_data[i] = kaiser_window_ext(i + offset, window_size, beta) * window_scale;
    }
#endif
  }
  for (int ch = 0; ch < 2; ch++, ch_mask >>= 1) {
    if ((ch_mask & 1U) == 0U) {
      continue;
    }
    float* tmp = (float*)spi_buffer;
    float* data = measured[ch][0];
    int i;
    for (i = 0; i < sweep_points; i++) {
#ifdef USE_FFT_WINDOW_BUFFER
      float w = kaiser_data[i];
#else
      float w = kaiser_window_ext(i + offset, window_size, beta) * window_scale;
#endif
      tmp[i * 2 + 0] = data[i * 2 + 0] * w;
      tmp[i * 2 + 1] = data[i * 2 + 1] * w;
    }
    for (; i < FFT_SIZE; i++) {
      tmp[i * 2 + 0] = 0.0f;
      tmp[i * 2 + 1] = 0.0f;
    }
    if (is_lowpass) {
      for (i = 1; i < sweep_points; i++) {
        tmp[(FFT_SIZE - i) * 2 + 0] = tmp[i * 2 + 0];
        tmp[(FFT_SIZE - i) * 2 + 1] = -tmp[i * 2 + 1];
      }
    }
    fft_inverse((float (*)[2])tmp);
    if (is_lowpass) {
      for (i = 0; i < sweep_points; i++) {
        tmp[i * 2 + 1] = 0.0f;
      }
    }
    if (domain_func == TD_FUNC_LOWPASS_STEP) {
      for (i = 1; i < sweep_points; i++) {
        tmp[i * 2 + 0] += tmp[(i - 1) * 2 + 0];
      }
    }
    memcpy(measured[ch], tmp, sizeof(measured[0]));
  }
}
