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

// Define LCD display driver and size
#if defined(NANOVNA_F303)
#define LCD_DRIVER_ST7796S
#define LCD_480x320
#else
// Used auto detect from ILI9341 or ST7789
#define LCD_DRIVER_ILI9341
#define LCD_DRIVER_ST7789
#define LCD_320x240
#endif

// Enable DMA mode for send data to LCD (Need enable HAL_USE_SPI in halconf.h)
#define __USE_DISPLAY_DMA__
// LCD or hardware allow change brightness, add menu item for this
#if defined(NANOVNA_F303)
#define __LCD_BRIGHTNESS__
#else
//#define __LCD_BRIGHTNESS__
#endif
// Use DAC (in H4 used for brightness used DAC, so need enable __LCD_BRIGHTNESS__ for it)
//#define __VNA_ENABLE_DAC__
// Allow enter to DFU from menu or command
#define __DFU_SOFTWARE_MODE__
// Add RTC clock support
#define __USE_RTC__
// Add RTC backup registers support
#define __USE_BACKUP__
// Add SD card support, requires RTC for timestamps
#define __USE_SD_CARD__
// Use unique serial string for USB
#define __USB_UID__
// If enabled serial in halconf.h, possible enable serial console control
//#define __USE_SERIAL_CONSOLE__
// Add show y grid line values option
#define __USE_GRID_VALUES__
// Add remote desktop option
#define __REMOTE_DESKTOP__
#if !defined(NANOVNA_F303)
#undef __REMOTE_DESKTOP__
#endif
// Add RLE8 compression capture image format
#define __CAPTURE_RLE8__
// Allow flip display
//#define __FLIP_DISPLAY__
// Add shadow on text in plot area (improve readable, but little slowdown render)
#define _USE_SHADOW_TEXT_
// Faster draw line in cell algorithm (better clipping and faster)
// #define __VNA_FAST_LINES__
// Use build in table for sin/cos calculation, allow save a lot of flash space (this table also use for FFT), max sin/cos error = 4e-7
#define __VNA_USE_MATH_TABLES__
// Use custom fast/compact approximation for some math functions in calculations (vna_ ...), use it carefully
#define __USE_VNA_MATH__
// Use cache for window function used by FFT (but need FFT_SIZE*sizeof(float) RAM)
//#define USE_FFT_WINDOW_BUFFER
// Enable data smooth option
#define __USE_SMOOTH__
// Enable optional change digit separator for locales (dot or comma, need for correct work some external software)
#define __DIGIT_SEPARATOR__
// Use table for frequency list (if disabled use real time calc)
//#define __USE_FREQ_TABLE__
// Enable DSP instruction (support only by Cortex M4 and higher)
#ifdef ARM_MATH_CM4
#define __USE_DSP__
#endif
// Add measure module option (allow made some measure calculations on data)
#define __VNA_MEASURE_MODULE__
// Add Z normalization feature
//#define __VNA_Z_RENORMALIZATION__

/*
 * Submodules defines
 */
#ifdef __USE_SD_CARD__
// Allow run commands from SD card (config.ini in root)
#define __SD_CARD_LOAD__
// Allow screenshots in TIFF format
#define __SD_CARD_DUMP_TIFF__
// Allow dump firmware to SD card
#define __SD_CARD_DUMP_FIRMWARE__
// Enable SD card file browser, and allow load files from it
#define __SD_FILE_BROWSER__
#endif

// If measure module enabled, add submodules
#ifdef __VNA_MEASURE_MODULE__
// Add LC match function
#define __USE_LC_MATCHING__
// Enable Series measure option
#define __S21_MEASURE__
// Enable S11 cable measure option
#define __S11_CABLE_MEASURE__
// Enable S11 resonance search option
#define __S11_RESONANCE_MEASURE__
#endif

/*
 * Hardware depends options for VNA
 */
#if defined(NANOVNA_F303)
// Define ADC sample rate in kilobyte (can be 48k, 96k, 192k, 384k)
//#define AUDIO_ADC_FREQ_K        768
//#define AUDIO_ADC_FREQ_K        384
#define AUDIO_ADC_FREQ_K        192
//#define AUDIO_ADC_FREQ_K        96
//#define AUDIO_ADC_FREQ_K        48

// Define sample count for one step measure
#define AUDIO_SAMPLES_COUNT   (48)
//#define AUDIO_SAMPLES_COUNT   (96)
//#define AUDIO_SAMPLES_COUNT   (192)

// Frequency offset, depend from AUDIO_ADC_FREQ settings (need aligned table)
// Use real time build table (undef for use constant, see comments)
// Constant tables build only for AUDIO_SAMPLES_COUNT = 48
#define USE_VARIABLE_OFFSET

// Maximum sweep point count (limit by flash and RAM size)
#define SWEEP_POINTS_MAX         401

#define AUDIO_ADC_FREQ_K1        384
#else
// Define ADC sample rate in kilobyte (can be 48k, 96k, 192k, 384k)
//#define AUDIO_ADC_FREQ_K        768
//#define AUDIO_ADC_FREQ_K        384
#define AUDIO_ADC_FREQ_K        192
//#define AUDIO_ADC_FREQ_K        96
//#define AUDIO_ADC_FREQ_K        48

// Define sample count for one step measure
#define AUDIO_SAMPLES_COUNT   (48)
//#define AUDIO_SAMPLES_COUNT   (96)
//#define AUDIO_SAMPLES_COUNT   (192)

// Frequency offset, depend from AUDIO_ADC_FREQ settings (need aligned table)
// Use real time build table (undef for use constant, see comments)
// Constant tables build only for AUDIO_SAMPLES_COUNT = 48
#define USE_VARIABLE_OFFSET

// Maximum sweep point count (limit by flash and RAM size)
#define SWEEP_POINTS_MAX         101
#endif
// Minimum sweep point count
#define SWEEP_POINTS_MIN         21

// Dirty hack for H4 ADC speed in version screen (Need for correct work NanoVNA-App)
#ifndef AUDIO_ADC_FREQ_K1
#define AUDIO_ADC_FREQ_K1        AUDIO_ADC_FREQ_K
#endif

/*
 * main.c
 */
// Minimum frequency set
#define FREQUENCY_MIN            600
// Maximum frequency set
#define FREQUENCY_MAX            2700000000U
// Frequency threshold (max frequency for si5351, harmonic mode after)
#define FREQUENCY_THRESHOLD      300000100U
// XTAL frequency on si5351
#define XTALFREQ                 26000000U
// Define i2c bus speed, add predefined for 400k, 600k, 900k
#define STM32_I2C_SPEED          900
// Define default src impedance for xtal calculations
#define MEASURE_DEFAULT_R        50.0f

// Add IF select menu in expert settings
#ifdef USE_VARIABLE_OFFSET
#define USE_VARIABLE_OFFSET_MENU
#endif

#if AUDIO_ADC_FREQ_K == 768
#define FREQUENCY_OFFSET_STEP    16000
// For 768k ADC    (16k step for 48 samples)
#define FREQUENCY_IF_K          8    // only  96 samples and variable table
//#define FREQUENCY_IF_K         12  // only 192 samples and variable table
//#define FREQUENCY_IF_K         16
//#define FREQUENCY_IF_K         32
//#define FREQUENCY_IF_K         48
//#define FREQUENCY_IF_K         64

#elif AUDIO_ADC_FREQ_K == 384
#define FREQUENCY_OFFSET_STEP    4000
// For 384k ADC    (8k step for 48 samples)
//#define FREQUENCY_IF_K          8
#define FREQUENCY_IF_K         12  // only 96 samples and variable table
//#define FREQUENCY_IF_K         16
//#define FREQUENCY_IF_K         20  // only 96 samples and variable table
//#define FREQUENCY_IF_K         24
//#define FREQUENCY_IF_K         32

#elif AUDIO_ADC_FREQ_K == 192
#define FREQUENCY_OFFSET_STEP    4000
// For 192k ADC (sin_cos table in dsp.c generated for 8k, 12k, 16k, 20k, 24k if change need create new table )
//#define FREQUENCY_IF_K          8
#define FREQUENCY_IF_K         12
//#define FREQUENCY_IF_K         16
//#define FREQUENCY_IF_K         20
//#define FREQUENCY_IF_K         24
//#define FREQUENCY_IF_K         28

#elif AUDIO_ADC_FREQ_K == 96
#define FREQUENCY_OFFSET_STEP    2000
// For 96k ADC (sin_cos table in dsp.c generated for 6k, 8k, 10k, 12k if change need create new table )
//#define FREQUENCY_IF_K          6
//#define FREQUENCY_IF_K          8
//#define FREQUENCY_IF_K         10
#define FREQUENCY_IF_K         12

#elif AUDIO_ADC_FREQ_K == 48
#define FREQUENCY_OFFSET_STEP    1000
// For 48k ADC (sin_cos table in dsp.c generated for 3k, 4k, 5k, 6k, if change need create new table )
//#define FREQUENCY_IF_K          3
//#define FREQUENCY_IF_K          4
//#define FREQUENCY_IF_K          5
#define FREQUENCY_IF_K          6
//#define FREQUENCY_IF_K          7
#endif
