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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime/runtime_features.h"
#include "ch.h"
#include "nanovna.h"

#define SWEEP_CH0_MEASURE (1U << 0)
#define SWEEP_CH1_MEASURE (1U << 1)
#define SWEEP_APPLY_EDELAY_S11 (1U << 2)
#define SWEEP_APPLY_EDELAY_S21 (1U << 3)
#define SWEEP_APPLY_S21_OFFSET (1U << 4)
#define SWEEP_APPLY_CALIBRATION (1U << 5)
#define SWEEP_USE_INTERPOLATION (1U << 6)
#define SWEEP_USE_RENORMALIZATION (1U << 7)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const float (*data)[2];
  uint16_t points;
  uint32_t generation;
} sweep_service_snapshot_t;

void sweep_service_init(void);
void sweep_service_wait_for_copy_release(void);
void sweep_service_begin_measurement(void);
void sweep_service_end_measurement(void);
uint32_t sweep_service_increment_generation(void);
uint32_t sweep_service_current_generation(void);
void sweep_service_wait_for_generation(void);
void sweep_service_reset_progress(void);
bool sweep_service_snapshot_acquire(uint8_t channel, sweep_service_snapshot_t *snapshot);
bool sweep_service_snapshot_release(const sweep_service_snapshot_t *snapshot);

void sweep_service_start_capture(systime_t delay_ticks);
bool sweep_service_wait_for_capture(void);
const audio_sample_t *sweep_service_rx_buffer(void);

#if ENABLED_DUMP_COMMAND
void sweep_service_prepare_dump(audio_sample_t *buffer, size_t count, int selection);
bool sweep_service_dump_ready(void);
#endif

uint16_t app_measurement_get_sweep_mask(void);
bool app_measurement_sweep(bool break_on_operation, uint16_t mask);
void sweep_wait_for_idle(void);
int app_measurement_set_frequency(freq_t freq);
void app_measurement_set_frequencies(freq_t start, freq_t stop, uint16_t points);
void app_measurement_reset(void);
void app_measurement_update_frequencies(void);
void app_measurement_transform_domain(uint16_t ch_mask);
void measurement_data_smooth(uint16_t ch_mask);

void set_smooth_factor(uint8_t factor);
uint8_t get_smooth_factor(void);

void sweep_service_set_sample_function(void (*func)(float *));

void i2s_lld_serve_rx_interrupt(uint32_t flags);

#ifdef __cplusplus
}
#endif
