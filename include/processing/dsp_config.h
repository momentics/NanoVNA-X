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





#ifndef __PROCESSING_DSP_CONFIG_H__
#define __PROCESSING_DSP_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "core/config_macros.h"

#define AUDIO_ADC_FREQ       (AUDIO_ADC_FREQ_K*1000)
#define FREQUENCY_OFFSET     (FREQUENCY_IF_K*1000)

// Speed of light const
#define SPEED_OF_LIGHT           299792458

// pi const
#define VNA_PI                   3.14159265358979323846f
#define VNA_TWOPI                6.28318530717958647692f

// Buffer contain left and right channel samples (need x2)
#define AUDIO_BUFFER_LEN      (AUDIO_SAMPLES_COUNT*2)

// Bandwidth depend from AUDIO_SAMPLES_COUNT and audio ADC frequency
// for AUDIO_SAMPLES_COUNT = 48 and ADC =  48kHz one measure give  48000/48=1000Hz
// for AUDIO_SAMPLES_COUNT = 48 and ADC =  96kHz one measure give  96000/48=2000Hz
// for AUDIO_SAMPLES_COUNT = 48 and ADC = 192kHz one measure give 192000/48=4000Hz
// Define additional measure count for menus
#if AUDIO_ADC_FREQ/AUDIO_SAMPLES_COUNT == 16000
#define BANDWIDTH_8000            (  1 - 1)
#define BANDWIDTH_4000            (  2 - 1)
#define BANDWIDTH_1000            (  8 - 1)
#define BANDWIDTH_333             ( 24 - 1)
#define BANDWIDTH_100             ( 80 - 1)
#define BANDWIDTH_30              (256 - 1)
#elif AUDIO_ADC_FREQ/AUDIO_SAMPLES_COUNT == 8000
#define BANDWIDTH_8000            (  1 - 1)
#define BANDWIDTH_4000            (  2 - 1)
#define BANDWIDTH_1000            (  8 - 1)
#define BANDWIDTH_333             ( 24 - 1)
#define BANDWIDTH_100             ( 80 - 1)
#define BANDWIDTH_30              (256 - 1)
#elif AUDIO_ADC_FREQ/AUDIO_SAMPLES_COUNT == 4000
#define BANDWIDTH_4000            (  1 - 1)
#define BANDWIDTH_2000            (  2 - 1)
#define BANDWIDTH_1000            (  4 - 1)
#define BANDWIDTH_333             ( 12 - 1)
#define BANDWIDTH_100             ( 40 - 1)
#define BANDWIDTH_30              (132 - 1)
#elif AUDIO_ADC_FREQ/AUDIO_SAMPLES_COUNT == 2000
#define BANDWIDTH_2000            (  1 - 1)
#define BANDWIDTH_1000            (  2 - 1)
#define BANDWIDTH_333             (  6 - 1)
#define BANDWIDTH_100             ( 20 - 1)
#define BANDWIDTH_30              ( 66 - 1)
#define BANDWIDTH_10              (200 - 1)
#elif AUDIO_ADC_FREQ/AUDIO_SAMPLES_COUNT == 1000
#define BANDWIDTH_1000            (  1 - 1)
#define BANDWIDTH_333             (  3 - 1)
#define BANDWIDTH_100             ( 10 - 1)
#define BANDWIDTH_30              ( 33 - 1)
#define BANDWIDTH_10              (100 - 1)
#endif

typedef int16_t audio_sample_t;

#ifdef __cplusplus
}
#endif

#endif // __PROCESSING_DSP_CONFIG_H__
