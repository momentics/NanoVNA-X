/*
 * Sweep service API: measurement orchestration and data snapshots.
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

#include "rf/driver/rf_measurement_driver.h"
#include "rf/sweep/sweep_orchestrator.h"
#include "platform/peripherals/si5351.h"
#include "nanovna.h"

// Typically defined in config or hal, ensuring visibility
#include "hal.h"

#ifndef DELAY_SWEEP_START
#define DELAY_SWEEP_START US2ST(100)
#endif
#ifndef DELAY_CHANNEL_CHANGE
#define DELAY_CHANNEL_CHANGE US2ST(100)
#endif

/*
 * Default implementation of RF Measurement Driver.
 * directly calls si5351, tlv320aic3204 and sweep_service functions.
 */

static bool default_measure_point(freq_t frequency, float* s11, float* s21) {
  // 1. Set Frequency
  // Use si5351 driver directly. current_props is global config.
  int delay = si5351_set_frequency(frequency, current_props._power);
  
  // Apply default delay if driver returns 0 (fast switch)
  if (delay == 0) {
    delay = DELAY_CHANNEL_CHANGE;
  }

  // 2. Wait for PLL settling
  uint8_t extra_cycles = si5351_take_settling_cycles();
  uint8_t total_cycles = extra_cycles + 1U;

  // Get sample processing function (DSP callback)
  sweep_sample_func_t sample = sweep_service_get_sample_function();
  if (sample == NULL) {
      return false;
  }

  // Run measurement cycles (usually 1, but more if PLL needs settling time)
  for (uint8_t cycle = 0; cycle < total_cycles; cycle++) {
    int current_delay = delay;
    // Extra delay at start of sweep/cycle?
    // Original logic: cycle_st_delay corresponds to DELAY_SWEEP_START on cycle 0
    int cycle_st_delay = (cycle == 0U) ? DELAY_SWEEP_START : 0;
    
    // --- Measure S11 (Channel 0) ---
    // Optimize: logic requires checking if s11 pointer is provided
    if (s11 != NULL) {
      tlv320aic3204_select(0);
      sweep_service_start_capture(current_delay + cycle_st_delay);
      
      // Update delay for next step in this cycle
      current_delay = DELAY_CHANNEL_CHANGE;
      
      if (!sweep_service_wait_for_capture()) {
        return false;
      }
      
      sample(s11);
      //s11->real = raw[0];
      //s11->imag = raw[1];
    } else {
        // If skipping S11, we might need to preserve timing or not?
        // In original code, skipping is mask-based. If mask implies NO ch0, we don't wait.
        // But if we skip, we still need to handle `current_delay` logic?
        // If s11 is NULL, we assume caller doesn't want it.
        // However, if we skip S11, the S21 measurement (if any) becomes the first action.
        // So S21 should take `delay + cycle_st_delay`.
        // But in my code `current_delay` is `delay`.
        // If s11 block runs, `current_delay` becomes `DELAY_CHANNEL_CHANGE`.
        // If s11 block skipped, `current_delay` remains `delay`.
        // So S21 gets `delay + cycle_st_delay`. This is CORRECT.
    }

    // --- Measure S21 (Channel 1) ---
    if (s21 != NULL) {
      tlv320aic3204_select(1);
      sweep_service_start_capture(current_delay + cycle_st_delay);
      // Note: cycle_st_delay added to both?
      // Original code: `sweep_service_start_capture(cycle_delay + cycle_st_delay);`
      // Where `cycle_st_delay` is (cycle==0)?start:0.
      // So YES, it is added to both if both run.
      // Wait, logically `cycle_st_delay` (Sweep Start Delay) should happen ONCE at start of frequency point?
      // Or start of Sweep?
      // "DELAY_SWEEP_START" suggests start of SWEEP.
      // In `app_measurement_sweep`, `st_delay` is init to `DELAY_SWEEP_START`.
      // It is NOT reset inside the loop.
      // So it is applied to EVERY frequency point if `cycle == 0`.
      // This suspects `DELAY_SWEEP_START` is actually "Measurement Wait Time" per point?
      // Or maybe it's 0 usually.
      // If it is small, it's fine.
      
      current_delay = DELAY_CHANNEL_CHANGE;

      if (!sweep_service_wait_for_capture()) {
        return false;
      }

      sample(s21);
      //s21->real = raw[0];
      //s21->imag = raw[1];
    }
  }

  return true;
}

static void default_cancel(void) {
    // No specific cancel action for default driver yet
}

static const rf_measurement_driver_t default_driver = {
    .measure_point = default_measure_point,
    .cancel = default_cancel
};

const rf_measurement_driver_t* rf_driver_get_default(void) {
    return &default_driver;
}
