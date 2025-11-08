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

#include "system/state_manager.h"

#include "nanovna.h"

#include "ch.h"

#include "app/sweep_service.h"
#include "platform/boards/stm32_peripherals.h"
#include "services/config_service.h"
#include "si5351.h"

#include <string.h>

#define SWEEP_STATE_AUTOSAVE_DELAY   MS2ST(750)
#define SWEEP_STATE_AUTOSAVE_MIN_GAP S2ST(3)

static bool sweep_state_dirty = false;
static systime_t sweep_state_deadline = 0;
static systime_t sweep_state_last_save = 0;

static const trace_t def_trace[TRACES_MAX] = {
    {TRUE, TRC_LOGMAG, 0, MS_RX, 10.0, NGRIDY - 1},
    {TRUE, TRC_LOGMAG, 1, MS_REIM, 10.0, NGRIDY - 1},
    {TRUE, TRC_SMITH, 0, MS_RX, 1.0, 0},
    {TRUE, TRC_PHASE, 1, MS_REIM, 90.0, NGRIDY / 2},
};

static const marker_t def_markers[MARKERS_MAX] = {
    {TRUE, 0, 10 * SWEEP_POINTS_MAX / 100 - 1, 0},
#if MARKERS_MAX > 1
    {FALSE, 0, 20 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 2
    {FALSE, 0, 30 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 3
    {FALSE, 0, 40 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 4
    {FALSE, 0, 50 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 5
    {FALSE, 0, 60 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 6
    {FALSE, 0, 70 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 7
    {FALSE, 0, 80 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
};

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
  memcpy(current_props._trace, def_trace, sizeof(def_trace));
  memcpy(current_props._markers, def_markers, sizeof(def_markers));
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
  caldata_save(active_calibration_slot());
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
