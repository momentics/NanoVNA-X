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

#pragma once

#include "nanovna.h"

int runtime_main(void);

void pause_sweep(void);
void resume_sweep(void);
void toggle_sweep(void);
bool need_interpolate(freq_t start, freq_t stop, uint16_t points);
void sweep_get_ordered(freq_t* start, freq_t* stop);

void set_power(uint8_t value);
void set_bandwidth(uint16_t bw_count);
uint32_t get_bandwidth_frequency(uint16_t bw_freq);
void set_sweep_points(uint16_t points);
void app_measurement_update_frequencies(void);

void set_sweep_frequency_internal(uint16_t type, freq_t freq, bool enforce_order);
void set_sweep_frequency(uint16_t type, freq_t freq);
void reset_sweep_frequency(void);

void eterm_set(int term, float re, float im);
void eterm_copy(int dst, int src);
void eterm_calc_es(void);
void eterm_calc_er(int sign);
void eterm_calc_et(void);

void cal_collect(uint16_t type);
void cal_done(void);

#ifdef __LCD_BRIGHTNESS__
void set_brightness(uint8_t brightness);
#else
static inline void set_brightness(uint8_t brightness) { (void)brightness; }
#endif

