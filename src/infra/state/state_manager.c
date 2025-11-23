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

#include "infra/state/state_manager.h"

#include "nanovna.h"

#include "ch.h"

#include "rf/sweep/sweep_orchestrator.h"
#include "platform/boards/stm32_peripherals.h"
#include "infra/storage/config_service.h"
#include "platform/peripherals/si5351.h"

#include <string.h>

#define SWEEP_STATE_AUTOSAVE_DELAY   MS2ST(750)
#define SWEEP_STATE_AUTOSAVE_MIN_GAP S2ST(3)

static bool sweep_state_dirty = false;
static systime_t sweep_state_deadline = 0;
static systime_t sweep_state_last_save = 0;

static void sanitize_rf_preferences(void) {
#ifdef USE_VARIABLE_OFFSET
  config._IF_freq = clamp_if_offset(config._IF_freq);
#endif
  config._harmonic_freq_threshold = clamp_harmonic_threshold(config._harmonic_freq_threshold);
}

static void load_default_properties(void) {
  current_props.magic = PROPERTIES_MAGIC;
  current_props._frequency0 = 50000;
  current_props._frequency1 = 2700000000U;
  current_props._var_freq = 0;
  current_props._sweep_points = POINTS_COUNT_DEFAULT;
  current_props._cal_frequency0 = 50000;
  current_props._cal_frequency1 = 2700000000U;
  current_props._cal_sweep_points = POINTS_COUNT_DEFAULT;
  current_props._cal_status = 0;
  current_props._trace[0] = (trace_t){true, TRC_LOGMAG, 0, MS_RX, 10.0f, NGRIDY - 1};
  current_props._trace[1] = (trace_t){true, TRC_LOGMAG, 1, MS_REIM, 10.0f, NGRIDY - 1};
  current_props._trace[2] = (trace_t){true, TRC_SMITH, 0, MS_RX, 1.0f, 0};
  current_props._trace[3] = (trace_t){true, TRC_PHASE, 1, MS_REIM, 90.0f, NGRIDY / 2};
  for (int i = 0; i < MARKERS_MAX; i++) {
    marker_t* marker = &current_props._markers[i];
    marker->enabled = (i == 0);
    marker->reserved = 0;
    marker->index = (int16_t)(10 * (i + 1) * SWEEP_POINTS_MAX / 100 - 1);
    marker->frequency = 0;
  }
  current_props._electrical_delay[0] = 0.0f;
  current_props._electrical_delay[1] = 0.0f;
  current_props._var_delay = 0.0f;
  current_props._s21_offset = 0.0f;
  current_props._portz = 50.0f;
  current_props._cal_load_r = 50.0f;
  current_props._velocity_factor = 70;
  current_props._current_trace = 0;
  current_props._active_marker = 0;
  current_props._previous_marker = MARKER_INVALID;
  current_props._mode = 0;
  current_props._reserved = 0;
  current_props._power = SI5351_CLK_DRIVE_STRENGTH_AUTO;
  current_props._cal_power = SI5351_CLK_DRIVE_STRENGTH_AUTO;
  current_props._measure = 0;
}

#ifdef __USE_BACKUP__
typedef union {
  struct {
    uint32_t points : 9;
    uint32_t bw : 9;
    uint32_t id : 4;
    uint32_t leveler : 3;
    uint32_t brightness : 7;
  };
  uint32_t v;
} backup_0;

static inline uint16_t active_calibration_slot(void) {
  uint16_t slot = lastsaveid;
  if (slot == NO_SAVE_SLOT || slot >= SAVEAREA_MAX) {
    slot = 0;
  }
  return slot;
}

void update_backup_data(void) {
  backup_0 bk = {.points = sweep_points,
                 .bw = config._bandwidth,
                 .id = lastsaveid,
                 .leveler = lever_mode,
                 .brightness = config._brightness};
  set_backup_data32(0, bk.v);
  set_backup_data32(1, frequency0);
  set_backup_data32(2, frequency1);
  set_backup_data32(3, var_freq);
  set_backup_data32(4, config._vna_mode);
}

static void load_settings(void) {
  load_default_properties();
  if (config_recall() == 0 && VNA_MODE(VNA_MODE_BACKUP)) {
    sanitize_rf_preferences();
    backup_0 bk = {.v = get_backup_data32(0)};
    if (bk.v != 0U) {
      if (bk.id < SAVEAREA_MAX && caldata_recall(bk.id) == 0) {
        sweep_points = bk.points;
        frequency0 = get_backup_data32(1);
        frequency1 = get_backup_data32(2);
        var_freq = get_backup_data32(3);
      } else {
        caldata_recall(0);
      }
      config._brightness = bk.brightness;
      lever_mode = bk.leveler;
      uint32_t backup_mode = get_backup_data32(4);
      if (backup_mode != 0xFFFFFFFFU) {
        config._vna_mode = backup_mode | (1 << VNA_MODE_BACKUP);
      } else {
        config._vna_mode |= (1 << VNA_MODE_BACKUP);
      }
      set_bandwidth(bk.bw);
    } else {
      caldata_recall(0);
    }
  } else {
    caldata_recall(0);
    sanitize_rf_preferences();
  }
  app_measurement_update_frequencies();
#ifdef __VNA_MEASURE_MODULE__
  plot_set_measure_mode(current_props._measure);
#endif
}

#else

void update_backup_data(void) {}

static void load_settings(void) {
  load_default_properties();
  config_recall();
  sanitize_rf_preferences();
  load_properties(0);
}

static inline uint16_t active_calibration_slot(void) {
  return 0;
}

#endif

void state_manager_init(void) {
  load_settings();
}

void state_manager_mark_dirty(void) {
#ifdef __USE_BACKUP__
  sweep_state_dirty = true;
  sweep_state_deadline = chVTGetSystemTimeX() + SWEEP_STATE_AUTOSAVE_DELAY;
#endif
}

void state_manager_force_save(void) {
#ifdef __USE_BACKUP__
  // Pause sweep completely during flash save to prevent conflicts
  bool was_sweeping = (sweep_mode & SWEEP_ENABLE) != 0;
  if (was_sweeping) {
    pause_sweep();
  }
  
  caldata_save(active_calibration_slot());
  
  // Resume sweep if it was running before
  if (was_sweeping) {
    resume_sweep();
  }
  
  sweep_state_dirty = false;
  sweep_state_last_save = chVTGetSystemTimeX();
#endif
}

void state_manager_service(void) {
#ifdef __USE_BACKUP__
  if (!VNA_MODE(VNA_MODE_BACKUP) || !sweep_state_dirty) {
    return;
  }
  const systime_t now = chVTGetSystemTimeX();
  if ((int32_t)(now - sweep_state_deadline) < 0) {
    return;
  }
  if ((int32_t)(now - sweep_state_last_save) < (int32_t)SWEEP_STATE_AUTOSAVE_MIN_GAP) {
    return;
  }
  state_manager_force_save();
#endif
}
