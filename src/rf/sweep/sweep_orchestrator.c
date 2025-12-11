/*
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

#include "rf/sweep/sweep_orchestrator.h"

#include "hal.h"
#include "platform/peripherals/si5351.h"
#include "ui/controller/ui_controller.h"

#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#ifdef __VNA_Z_RENORMALIZATION__
#include "../analysis/vna_renorm.c"
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

static uint8_t smooth_factor = 0;
static void (*volatile sample_func)(float* gamma) = NULL;

void sweep_service_set_sample_function(void (*func)(float*)) {
  if (func == NULL) {
    func = calculate_gamma;
  }
  osalSysLock();
  sample_func = func;
  osalSysUnlock();
}

static inline bool sweep_ui_input_pending(void) {
  uint8_t pending = ui_controller_pending_requests();
  if (pending & UI_CONTROLLER_REQUEST_CONSOLE) {
    ui_controller_release_requests(UI_CONTROLLER_REQUEST_CONSOLE);
    return true;
  }
  return (pending & (UI_CONTROLLER_REQUEST_TOUCH | UI_CONTROLLER_REQUEST_LEVER)) != 0;
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

#define SWEEP_UI_INPUT_SLICE_POINTS 1U
#define SWEEP_UI_IDLE_SLICE_POINTS ((uint16_t)SWEEP_POINTS_MAX)
#define SWEEP_UI_TIMESLICE_TICKS MS2ST(8U)

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

static inline bool sweep_timeslice_expired(systime_t slice_start) {
  if ((SWEEP_UI_TIMESLICE_TICKS == 0) || slice_start == 0U) {
    return false;
  }
  return chVTTimeElapsedSinceX(slice_start) >= SWEEP_UI_TIMESLICE_TICKS;
}

static inline uint16_t sweep_points_budget(bool break_on_operation) {
  if (!break_on_operation) {
    return UINT16_MAX;
  }
  if (sweep_ui_input_pending()) {
    return SWEEP_UI_INPUT_SLICE_POINTS;
  }
  return SWEEP_UI_IDLE_SLICE_POINTS;
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
  if (wait > config._bandwidth + 2U) {
    // More than expected buffers - this shouldn't happen, but handle gracefully
    --wait_count;
    return;
  } else if (wait == config._bandwidth + 2U) {
    // First buffer after reset - always reset accumulator to clear any initial noise
    reset_dsp_accumerator();
  } else {
    // Process actual measurement data
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
      // Avoid resetting DSP accumulator here to preserve partially accumulated data
      wait_count = 0;
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
  // S11m' = S11m - Ed
  // S11a = S11m' / (Er + Es S11m')
  float s11mr = data[0] - c_data[ETERM_ED][0];
  float s11mi = data[1] - c_data[ETERM_ED][1];
  float err = c_data[ETERM_ER][0] + s11mr * c_data[ETERM_ES][0] - s11mi * c_data[ETERM_ES][1];
  float eri = c_data[ETERM_ER][1] + s11mr * c_data[ETERM_ES][1] + s11mi * c_data[ETERM_ES][0];
  float sq = err*err + eri*eri;
  if (sq != 0.0f) {
    float inv = 1.0f / sq;
    data[0] = (s11mr * err + s11mi * eri) * inv;
    data[1] = (s11mi * err - s11mr * eri) * inv;
  } else {
    data[0] = 0.0f;
    data[1] = 0.0f;
  }
}

static void apply_ch1_error_term(float data[4], float c_data[CAL_TYPE_COUNT][2]) {
  // CAUTION: Et is inversed for efficiency
  // S21a = (S21m - Ex) * Et
  float s21mr = data[2] - c_data[ETERM_EX][0];
  float s21mi = data[3] - c_data[ETERM_EX][1];
  // Apply transmission tracking correction (ET is inverted for efficiency)
  data[2] = s21mr * c_data[ETERM_ET][0] - s21mi * c_data[ETERM_ET][1];
  data[3] = s21mi * c_data[ETERM_ET][0] + s21mr * c_data[ETERM_ET][1];
  
  // Enhanced Response: S21a *= 1 - Es * S11a (if enabled)
  if (cal_status & CALSTAT_ENHANCED_RESPONSE) {
    float esr = 1.0f - (c_data[ETERM_ES][0] * data[0] - c_data[ETERM_ES][1] * data[1]);
    float esi = 0.0f - (c_data[ETERM_ES][1] * data[0] + c_data[ETERM_ES][0] * data[1]);
    float re = data[2];
    float im = data[3];
    data[2] = esr * re - esi * im;
    data[3] = esi * re + esr * im;
  }
}

// Helper function declaration
static bool process_sweep_channel(uint8_t channel, int cycle_delay, int cycle_st_delay,
                                 bool final_cycle, uint16_t mask, int interpolation_idx,
                                 freq_t frequency, float *data, float c_data[CAL_TYPE_COUNT][2]);

static void cal_interpolate(int idx, freq_t f, float data[CAL_TYPE_COUNT][2]) {
  uint16_t src_points = cal_sweep_points - 1;
  if (idx >= 0) {
    // Direct point copy if index provided
    for (uint16_t eterm = 0; eterm < CAL_TYPE_COUNT; eterm++) {
      data[eterm][0] = cal_data[eterm][idx][0];
      data[eterm][1] = cal_data[eterm][idx][1];
    }
    return;
  }
  
  if (f <= cal_frequency0) {
    idx = 0;
    for (uint16_t eterm = 0; eterm < CAL_TYPE_COUNT; eterm++) {
      data[eterm][0] = cal_data[eterm][0][0];
      data[eterm][1] = cal_data[eterm][0][1];
    }
    return;
  }
  
  if (f >= cal_frequency1) {
    idx = src_points;
    for (uint16_t eterm = 0; eterm < CAL_TYPE_COUNT; eterm++) {
      data[eterm][0] = cal_data[eterm][src_points][0];
      data[eterm][1] = cal_data[eterm][src_points][1];
    }
    return;
  }
  
  // Calculate k for linear interpolation
  freq_t span = cal_frequency1 - cal_frequency0;
  idx = (uint64_t)(f - cal_frequency0) * (uint64_t)src_points / span;
  uint64_t v = (uint64_t)span * idx + src_points/2;
  freq_t src_f0 = cal_frequency0 + (v       ) / src_points;
  freq_t src_f1 = cal_frequency0 + (v + span) / src_points;
  
  freq_t delta = src_f1 - src_f0;
  // Not need interpolate
  if (f == src_f0) {
    for (uint16_t eterm = 0; eterm < CAL_TYPE_COUNT; eterm++) {
      data[eterm][0] = cal_data[eterm][idx][0];
      data[eterm][1] = cal_data[eterm][idx][1];
    }
    return;
  }
  
  float k = (delta == 0) ? 0.0f : (float)(f - src_f0) / delta;

  // avoid glitch between freqs in different harmonics mode
  uint32_t hf0 = si5351_get_harmonic_lvl(src_f0);
  if (hf0 != si5351_get_harmonic_lvl(src_f1)) {
    // f in prev harmonic, need extrapolate from prev 2 points
    if (hf0 == si5351_get_harmonic_lvl(f)){
      if (idx < 1) {
         // point limit, direct copy (matches copy logic above)
        for (uint16_t eterm = 0; eterm < CAL_TYPE_COUNT; eterm++) {
          data[eterm][0] = cal_data[eterm][idx][0];
          data[eterm][1] = cal_data[eterm][idx][1];
        }
        return;
      }
      idx--;
      k+= 1.0f;
    }
    // f in next harmonic, need extrapolate from next 2 points
    else {
      if (idx >= src_points - 1) {
        // point limit (cannot extrapolate from next), direct copy current
        for (uint16_t eterm = 0; eterm < CAL_TYPE_COUNT; eterm++) {
          data[eterm][0] = cal_data[eterm][idx][0];
          data[eterm][1] = cal_data[eterm][idx][1];
        }
        return;
      }
      idx++;
      k-= 1.0f;
    }
  }

  // Interpolate by k
  for (uint16_t eterm = 0; eterm < CAL_TYPE_COUNT; eterm++) {
    data[eterm][0] = cal_data[eterm][idx][0] + k * (cal_data[eterm][idx+1][0] - cal_data[eterm][idx][0]);
    data[eterm][1] = cal_data[eterm][idx][1] + k * (cal_data[eterm][idx+1][1] - cal_data[eterm][idx][1]);
  }
}

// Static buffers to reduce stack usage in app_measurement_sweep
static float sweep_data[4];
static float sweep_cal_data[CAL_TYPE_COUNT][2];

bool app_measurement_sweep(bool break_on_operation, uint16_t mask) {
  sweep_in_progress = true;
  sweep_service_wait_for_copy_release();
  if (p_sweep >= sweep_points || !break_on_operation) {
    sweep_reset_progress();
    sweep_progress_end();
    sweep_led_end();
  }
  if (break_on_operation && mask == 0U) {
    sweep_progress_end();
    sweep_led_end();
    sweep_in_progress = false;
    return false;
  }
  bool completed = false;
  int delay = 0;
  int st_delay = DELAY_SWEEP_START;
  int interpolation_idx = p_sweep;
  bool show_progress = config._bandwidth >= BANDWIDTH_100;
  uint16_t batch_budget = sweep_points_budget(break_on_operation);
  uint16_t processed = 0;
  float offset = 1.0f;
  if (mask & SWEEP_APPLY_S21_OFFSET) {
    offset = vna_expf(s21_offset * (logf(10.0f) / 20.0f));
  }

  if (p_sweep == 0U) {
    sweep_prepare_led_and_progress(show_progress);
  }

  const systime_t slice_start = break_on_operation ? chVTGetSystemTimeX() : 0;

  for (; p_sweep < sweep_points; p_sweep++) {
    if (processed >= batch_budget) {
      break;
    }
    if (break_on_operation && sweep_ui_input_pending()) {
      break;
    }
    if (sweep_timeslice_expired(slice_start)) {
      break;
    }
    freq_t frequency = get_frequency(p_sweep);
    uint8_t extra_cycles = 0U;
    if (mask & (SWEEP_CH0_MEASURE | SWEEP_CH1_MEASURE)) {
      delay = app_measurement_set_frequency(frequency);
      interpolation_idx = (mask & SWEEP_USE_INTERPOLATION) ? -1 : (int)p_sweep;
      extra_cycles = si5351_take_settling_cycles();
    }
    uint8_t total_cycles = extra_cycles + 1U;

    // Process each cycle - reduced nesting by extracting logic
    for (uint8_t cycle = 0; cycle < total_cycles; cycle++) {
      bool final_cycle = (cycle == total_cycles - 1U);
      int cycle_delay = delay;
      int cycle_st_delay = (cycle == 0U) ? st_delay : 0;

      // Process channel 0 if needed
      if (mask & SWEEP_CH0_MEASURE) {
        if (!process_sweep_channel(0, cycle_delay, cycle_st_delay, final_cycle,
                                  mask, interpolation_idx, frequency,
                                  sweep_data, sweep_cal_data)) {
          goto capture_failure;
        }
        // Store results
        measured[0][p_sweep][0] = sweep_data[0];
        measured[0][p_sweep][1] = sweep_data[1];
      }

      // Process channel 1 if needed
      if (mask & SWEEP_CH1_MEASURE) {
        // For channel 1, we may need to interpolate again if channel 0 wasn't processed
        if (final_cycle && (mask & SWEEP_APPLY_CALIBRATION) && (mask & SWEEP_CH0_MEASURE) == 0U) {
          cal_interpolate(interpolation_idx, frequency, sweep_cal_data);
        }

        if (!process_sweep_channel(1, cycle_delay, cycle_st_delay, final_cycle,
                                  mask, interpolation_idx, frequency,
                                  sweep_data, sweep_cal_data)) {
          goto capture_failure;
        }

        // Apply S21 offset if needed
        if (mask & SWEEP_APPLY_S21_OFFSET) {
          apply_offset(&sweep_data[2], offset);
        }

        // Store results
        measured[1][p_sweep][0] = sweep_data[2];
        measured[1][p_sweep][1] = sweep_data[3];
      }
    }

#ifdef __VNA_Z_RENORMALIZATION__
    if (mask & SWEEP_USE_RENORMALIZATION) {
      // Use the sweep_data buffer for renormalization
      // Note: This may need to process both channels differently
      // For now, pass the first two elements for ch0 and next two for ch1
      apply_renormalization(sweep_data, mask);
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
  sweep_in_progress = false;
  return completed;

capture_failure:
  sweep_reset_progress();
  sweep_progress_end();
  sweep_led_end();
  sweep_in_progress = false;
  return false;
}

void sweep_wait_for_idle(void) {
  while (sweep_in_progress) {
    chThdSleepMilliseconds(1);
  }
}

// Helper function to process a single measurement channel
static bool process_sweep_channel(uint8_t channel, int cycle_delay, int cycle_st_delay,
                                 bool final_cycle, uint16_t mask, int interpolation_idx,
                                 freq_t frequency, float *data, float c_data[CAL_TYPE_COUNT][2]) {
  tlv320aic3204_select(channel);
  sweep_service_start_capture(cycle_delay + cycle_st_delay);
  cycle_delay = DELAY_CHANNEL_CHANGE;

  if (final_cycle && (mask & SWEEP_APPLY_CALIBRATION)) {
    cal_interpolate(interpolation_idx, frequency, c_data);
  }

  if (!sweep_service_wait_for_capture()) {
    return false;
  }

  void (*sample_cb)(float*) = sample_func;
  // Process correct part of data array for the channel
  if (channel == 0) {
    sample_cb(&data[0]);  // Process S11 data[0], data[1]
  } else {
    sample_cb(&data[2]);  // Process S21 data[2], data[3]
  }

  if (final_cycle && (mask & SWEEP_APPLY_CALIBRATION)) {
    if (channel == 0) {
      apply_ch0_error_term(data, c_data);
    } else {
      apply_ch1_error_term(data, c_data);
    }
  }

  if (channel == 0 && (mask & SWEEP_APPLY_EDELAY_S11)) {
    apply_edelay(electrical_delayS11 * frequency, &data[0]);
  } else if (channel == 1 && (mask & SWEEP_APPLY_EDELAY_S21)) {
    apply_edelay(electrical_delayS21 * frequency, &data[2]);
  }

  return true;
}

void measurement_data_smooth(uint16_t ch_mask) {
  if (smooth_factor == 0U) {
    return;
  }
  if (sweep_points <= 2U) {
    return;
  }
  float (*smooth_func)(float, float, float) =
      VNA_MODE(VNA_MODE_SMOOTH) ? arifmetic_mean : geometry_mean;
  for (int ch = 0; ch < 2; ch++, ch_mask >>= 1) {
    if ((ch_mask & 1U) == 0U) {
      continue;
    }
    uint32_t max_passes = (sweep_points > 2U) ? (sweep_points - 2U) : 0U;
    uint32_t count = 1U << (smooth_factor - 1U);
    if (count == 0U)
      count = 1U;
    if (count > max_passes)
      count = max_passes;
    if (count == 0U)
      continue;
    float* data = measured[ch][0];
    for (uint32_t n = 0; n < count; n++) {
      float prev_re = data[0];
      float prev_im = data[1];
      for (uint32_t j = 1; j < sweep_points - 1U; j++) {
        float old_re = data[2 * j];
        float old_im = data[2 * j + 1];
        data[2 * j] = smooth_func(prev_re, data[2 * j], data[2 * j + 2]);
        data[2 * j + 1] = smooth_func(prev_im, data[2 * j + 1], data[2 * j + 3]);
        prev_re = old_re;
        prev_im = old_im;
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
  // Use dynamic delay returned by si5351_set_frequency which accounts for PLL settling time
  // Different values are returned based on frequency range changes, PLL resets, etc.
  // This ensures proper synchronization between frequency setting and measurement
  int delay = si5351_set_frequency(freq, current_props._power);
  
  // Use the original delay calculation from DiSlord firmware for proper timing
  // If no specific delay returned, use default channel change delay
  if (delay == 0) {
    delay = DELAY_CHANNEL_CHANGE;
  }
  
  return delay;
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
