#pragma once

// Define LCD display driver and size
#if defined(NANOVNA_F303)
#define LCD_DRIVER_ST7796S
#define LCD_480X320
#else
// Used auto detect from ILI9341 or ST7789
#define LCD_DRIVER_ILI9341
#define LCD_DRIVER_ST7789
#define LCD_320X240
#endif

// Enable DMA mode for send data to LCD (Need enable HAL_USE_SPI in halconf.h)
#define USE_DISPLAY_DMA
// LCD or hardware allow change brightness, add menu item for this
#if defined(NANOVNA_F303)
#define LCD_BRIGHTNESS
#else
// #define LCD_BRIGHTNESS
#endif
// Use DAC (in H4 used for brightness used DAC, so need enable LCD_BRIGHTNESS for it)
// #define VNA_ENABLE_DAC
// Allow enter to DFU from menu or command
#define DFU_SOFTWARE_MODE
// Add RTC clock support
#define USE_RTC
// Add RTC backup registers support
#define USE_BACKUP
// Add SD card support, requires RTC for timestamps
#define USE_SD_CARD
// Use unique serial string for USB
#define USB_UID
// If enabled serial in halconf.h, possible enable serial console control
// #define USE_SERIAL_CONSOLE
// Add show y grid line values option
#define USE_GRID_VALUES
// Add remote desktop option
#define REMOTE_DESKTOP
#if !defined(NANOVNA_F303)
#undef REMOTE_DESKTOP
#endif
// Add RLE8 compression capture image format
#define CAPTURE_RLE8
// Allow flip display
// #define FLIP_DISPLAY
// Add shadow on text in plot area (improve readable, but little slowdown render)
#define USE_SHADOW_TEXT
// Faster draw line in cell algorithm (better clipping and faster)
// #define VNA_FAST_LINES
// Use build in table for sin/cos calculation, allow save a lot of flash space (this table also use
// for FFT), max sin/cos error = 4e-7
#define VNA_USE_MATH_TABLES
// Use custom fast/compact approximation for some math functions in calculations (vna_ ...), use it
// carefully
#define USE_VNA_MATH
// Use cache for window function used by FFT (but need FFT_SIZE*sizeof(float) RAM)
// #define USE_FFT_WINDOW_BUFFER
// Enable data smooth option
#define USE_SMOOTH
// Enable optional change digit separator for locales (dot or comma, need for correct work some
// external software)
#define DIGIT_SEPARATOR
// Use table for frequency list (if disabled use real time calc)
// #define USE_FREQ_TABLE
// Enable DSP instruction (support only by Cortex M4 and higher)
#ifdef ARM_MATH_CM4
#define USE_DSP
#endif
// Add measure module option (allow made some measure calculations on data)
#define VNA_MEASURE_MODULE
// Add Z normalization feature
// #define VNA_Z_RENORMALIZATION

/*
 * Submodules defines
 */
#ifdef USE_SD_CARD
// Allow run commands from SD card (config.ini in root)
#define SD_CARD_LOAD
// Allow screenshots in TIFF format
#define SD_CARD_DUMP_TIFF
// Allow dump firmware to SD card
#define SD_CARD_DUMP_FIRMWARE
// Enable SD card file browser, and allow load files from it
#define SD_FILE_BROWSER
#endif

// If measure module enabled, add submodules
#ifdef VNA_MEASURE_MODULE
// Add LC match function
#define USE_LC_MATCHING
// Enable Series measure option
#define S21_MEASURE
// Enable S11 cable measure option
#define S11_CABLE_MEASURE
// Enable S11 resonance search option
#define S11_RESONANCE_MEASURE
#endif

/*
 * Hardware depends options for VNA
 */
#if defined(NANOVNA_F303)
// Define ADC sample rate in kilobyte (can be 48k, 96k, 192k, 384k)
// #define AUDIO_ADC_FREQ_K        768
// #define AUDIO_ADC_FREQ_K        384
#define AUDIO_ADC_FREQ_K 192
// #define AUDIO_ADC_FREQ_K        96
// #define AUDIO_ADC_FREQ_K        48

// Define sample count for one step measure
#define AUDIO_SAMPLES_COUNT (48)
// #define AUDIO_SAMPLES_COUNT   (96)
// #define AUDIO_SAMPLES_COUNT   (192)

// Frequency offset, depend from AUDIO_ADC_FREQ settings (need aligned table)
// Use real time build table (undef for use constant, see comments)
// Constant tables build only for AUDIO_SAMPLES_COUNT = 48
#define USE_VARIABLE_OFFSET

// Maximum sweep point count (limit by flash and RAM size)
#define SWEEP_POINTS_MAX 401

#define AUDIO_ADC_FREQ_K1 384
#else
// Define ADC sample rate in kilobyte (can be 48k, 96k, 192k, 384k)
// #define AUDIO_ADC_FREQ_K        768
// #define AUDIO_ADC_FREQ_K        384
#define AUDIO_ADC_FREQ_K 192
// #define AUDIO_ADC_FREQ_K        96
// #define AUDIO_ADC_FREQ_K        48

// Define sample count for one step measure
#define AUDIO_SAMPLES_COUNT (48)
// #define AUDIO_SAMPLES_COUNT   (96)
// #define AUDIO_SAMPLES_COUNT   (192)

// Frequency offset, depend from AUDIO_ADC_FREQ settings (need aligned table)
// Use real time build table (undef for use constant, see comments)
// Constant tables build only for AUDIO_SAMPLES_COUNT = 48
#define USE_VARIABLE_OFFSET

// Maximum sweep point count (limit by flash and RAM size)
#define SWEEP_POINTS_MAX 101
#endif
// Minimum sweep point count
#define SWEEP_POINTS_MIN 21

// Dirty hack for H4 ADC speed in version screen (Need for correct work NanoVNA-App)
#ifndef AUDIO_ADC_FREQ_K1
#define AUDIO_ADC_FREQ_K1 AUDIO_ADC_FREQ_K
#endif

/*
 * main.c
 */
// Minimum frequency set
#define FREQUENCY_MIN 600
// Maximum frequency set
#define FREQUENCY_MAX 2700000000U
// Frequency threshold (max frequency for si5351, harmonic mode after)
#define FREQUENCY_THRESHOLD 300000100U
// XTAL frequency on si5351
#define XTALFREQ 26000000U
// Define i2c bus speed, add predefined for 400k, 600k, 900k
#define STM32_I2C_SPEED 900
// Define default src impedance for xtal calculations
#define MEASURE_DEFAULT_R 50.0f

// Add IF select menu in expert settings
#ifdef USE_VARIABLE_OFFSET
#define USE_VARIABLE_OFFSET_MENU
#endif

#if AUDIO_ADC_FREQ_K == 768
#define FREQUENCY_OFFSET_STEP 16000
// For 768k ADC    (16k step for 48 samples)
#define FREQUENCY_IF_K 8 // only  96 samples and variable table
// #define FREQUENCY_IF_K         12  // only 192 samples and variable table
// #define FREQUENCY_IF_K         16
// #define FREQUENCY_IF_K         32
// #define FREQUENCY_IF_K         48
// #define FREQUENCY_IF_K         64

#elif AUDIO_ADC_FREQ_K == 384
#define FREQUENCY_OFFSET_STEP 4000
// For 384k ADC    (8k step for 48 samples)
// #define FREQUENCY_IF_K          8
#define FREQUENCY_IF_K 12 // only 96 samples and variable table
// #define FREQUENCY_IF_K         16
// #define FREQUENCY_IF_K         20  // only 96 samples and variable table
// #define FREQUENCY_IF_K         24
// #define FREQUENCY_IF_K         32

#elif AUDIO_ADC_FREQ_K == 192
#define FREQUENCY_OFFSET_STEP 4000
// For 192k ADC (sin_cos table in dsp.c generated for 8k, 12k, 16k, 20k, 24k if change need create
// new table )
// #define FREQUENCY_IF_K          8
#define FREQUENCY_IF_K 12
// #define FREQUENCY_IF_K         16
// #define FREQUENCY_IF_K         20
// #define FREQUENCY_IF_K         24
// #define FREQUENCY_IF_K         28

#elif AUDIO_ADC_FREQ_K == 96
#define FREQUENCY_OFFSET_STEP 2000
// For 96k ADC (sin_cos table in dsp.c generated for 6k, 8k, 10k, 12k if change need create new
// table )
// #define FREQUENCY_IF_K          6
// #define FREQUENCY_IF_K          8
// #define FREQUENCY_IF_K         10
#define FREQUENCY_IF_K 12

#elif AUDIO_ADC_FREQ_K == 48
#define FREQUENCY_OFFSET_STEP 1000
// For 48k ADC (sin_cos table in dsp.c generated for 3k, 4k, 5k, 6k, if change need create new table
// )
// #define FREQUENCY_IF_K          3
// #define FREQUENCY_IF_K          4
// #define FREQUENCY_IF_K          5
#define FREQUENCY_IF_K 6
// #define FREQUENCY_IF_K          7
#endif
