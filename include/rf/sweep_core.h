/*
 * Copyright (c) 2019-2020, Dmitry (DiSlord) dislordlive@gmail.com
 * Based on TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "core/data_types.h"
#include "core/config_macros.h"
#include "platform/boards/stm32_peripherals.h"
#include "platform/hal.h"

#define AUDIO_ADC_FREQ (AUDIO_ADC_FREQ_K * 1000)
#define FREQUENCY_OFFSET (FREQUENCY_IF_K * 1000)

// Speed of light const
#define SPEED_OF_LIGHT 299792458


// Apply calibration after made sweep, (if set 1, then calibration move out from sweep cycle)
#define APPLY_CALIBRATION_AFTER_SWEEP 0

// Optional sweep point (in UI menu)
#if SWEEP_POINTS_MAX >= 401
#define POINTS_SET_COUNT 5
#define POINTS_SET                                                                                 \
  { 51, 101, 201, 301, SWEEP_POINTS_MAX }
#define POINTS_COUNT_DEFAULT SWEEP_POINTS_MAX
#elif SWEEP_POINTS_MAX >= 301
#define POINTS_SET_COUNT 4
#define POINTS_SET                                                                                 \
  { 51, 101, 201, SWEEP_POINTS_MAX }
#define POINTS_COUNT_DEFAULT SWEEP_POINTS_MAX
#elif SWEEP_POINTS_MAX >= 201
#define POINTS_SET_COUNT 3
#define POINTS_SET                                                                                 \
  { 51, 101, SWEEP_POINTS_MAX }
#define POINTS_COUNT_DEFAULT SWEEP_POINTS_MAX
#elif SWEEP_POINTS_MAX >= 101
#define POINTS_SET_COUNT 2
#define POINTS_SET                                                                                 \
  { 51, SWEEP_POINTS_MAX }
#define POINTS_COUNT_DEFAULT SWEEP_POINTS_MAX
#endif

#if SWEEP_POINTS_MAX <= 256
#define FFT_SIZE 256
#elif SWEEP_POINTS_MAX <= 512
#define FFT_SIZE 512
#endif

freq_t get_frequency(uint16_t idx);
freq_t get_frequency_step(void);

void set_marker_index(int m, int idx);
freq_t get_marker_frequency(int marker);

void reset_sweep_frequency(void);
void set_sweep_frequency(uint16_t type, freq_t frequency);
void set_sweep_frequency_internal(uint16_t type, freq_t freq, bool enforce_order);

void set_bandwidth(uint16_t bw_count);
uint32_t get_bandwidth_frequency(uint16_t bw_freq);

void set_power(uint8_t value);

void set_smooth_factor(uint8_t factor);
uint8_t get_smooth_factor(void);

void pause_sweep(void);
void resume_sweep(void);
void toggle_sweep(void);
int load_properties(uint32_t id);

void set_sweep_points(uint16_t points);

#ifdef REMOTE_DESKTOP
// State flags for remote touch state
void remote_touch_set(uint16_t state, int16_t x, int16_t y);
void send_region(remote_region_t *rd, uint8_t *buf, uint16_t size);
#endif

void app_measurement_update_frequencies(void);
bool need_interpolate(freq_t start, freq_t stop, uint16_t points);
void sweep_get_ordered(freq_t *start, freq_t *stop);

#define SWEEP_ENABLE 0x01
#define SWEEP_ONCE 0x02
#define SWEEP_BINARY 0x08
#define SWEEP_REMOTE 0x40
#define SWEEP_UI_MODE 0x80

// Generator ready delays
#if defined(NANOVNA_F303)
#define DELAY_BAND_1_2 US2ST(100)       // Delay for bands 1-2
#define DELAY_BAND_3_4 US2ST(120)       // Delay for bands 3-4
#define DELAY_BANDCHANGE US2ST(2000)    // Band changes need set additional delay after reset PLL
#define DELAY_CHANNEL_CHANGE US2ST(100) // Switch channel delay
#define DELAY_SWEEP_START US2ST(2000)   // Delay at sweep start
#define DELAY_RESET_PLL_BEFORE 0   //    0 (0 for disabled)
#define DELAY_RESET_PLL_AFTER 4000 // 4000 (0 for disabled)
#else
#define DELAY_BAND_1_2 US2ST(100)       // 0 Delay for bands 1-2
#define DELAY_BAND_3_4 US2ST(140)       // 1 Delay for bands 3-4
#define DELAY_BANDCHANGE US2ST(5000)    // 2 Band changes need set additional delay after reset PLL
#define DELAY_CHANNEL_CHANGE US2ST(100) // 3 Switch channel delay
#define DELAY_SWEEP_START US2ST(100)    // 4 Delay at sweep start
#define DELAY_RESET_PLL_BEFORE 0        // 5    0 (0 for disabled)
#define DELAY_RESET_PLL_AFTER 4000      // 6 4000 (0 for disabled)
#endif
