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

#ifndef RF_MEASUREMENT_DRIVER_H
#define RF_MEASUREMENT_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

// Using existing types if possible, or defining minimal ones
// Assuming freq_t is defined broadly, usually in nanovna.h or similar config
#include "nanovna.h" 

typedef struct {
    float real;
    float imag;
} complex_float_t;

typedef struct {
    // Measure a single frequency point
    // This function handles:
    // 1. Setting the generator frequency
    // 2. Waiting for settling time
    // 3. Measuring S11 (Channel 0) - writes 2 floats (Real, Imag) to s11
    // 4. Measuring S21 (Channel 1) - writes 2 floats (Real, Imag) to s21
    // Returns true if successful, false on error/abort.
    bool (*measure_point)(freq_t frequency, float* s11, float* s21);
    
    // Cancel any ongoing hardware operation.
    // Intent: safely stop pending DMA or waits during user interruption.
    void (*cancel)(void);
} rf_measurement_driver_t;

// Accessor for the default driver instance
const rf_measurement_driver_t* rf_driver_get_default(void);

#endif // RF_MEASUREMENT_DRIVER_H
