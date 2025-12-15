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
#include "ch.h"
#include "infra/event/event_bus.h"
#include <stdalign.h>
#include <stdarg.h>
#include <stdint.h>

// Define LCD display driver and size
#include "core/config_macros.h"
#include "runtime/runtime_features.h"

/*
 * CPU Hardware depend functions declaration
 */
#include "platform/boards/stm32_peripherals.h"
#include "platform/hal.h"

#define AUDIO_ADC_FREQ (AUDIO_ADC_FREQ_K * 1000)
#define FREQUENCY_OFFSET (FREQUENCY_IF_K * 1000)

// Apply calibration after made sweep, (if set 1, then calibration move out from sweep cycle)
#define APPLY_CALIBRATION_AFTER_SWEEP 0

// Speed of light const
#define SPEED_OF_LIGHT 299792458

// pi const
#define VNA_PI 3.14159265358979323846f
#define VNA_TWOPI 6.28318530717958647692f

#include "core/globals.h"
#include "ui/ui_style.h"
#include "ui/core/ui_core.h"
#include "processing/calibration.h"

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

void set_bandwidth(uint16_t bw_count);
uint32_t get_bandwidth_frequency(uint16_t bw_freq);

void set_power(uint8_t value);

void set_smooth_factor(uint8_t factor);
uint8_t get_smooth_factor(void);

int32_t my_atoi(const char *p);
uint32_t my_atoui(const char *p);
float my_atof(const char *p);
bool strcmpi(const char *t1, const char *t2);
int get_str_index(const char *v, const char *list);
int parse_line(char *line, char *args[], int max_cnt);
void swap_bytes(uint16_t *buf, int size);
int packbits(char *source, char *dest, int size);
void delay_8t(uint32_t cycles);
inline void delay_microseconds(uint32_t us) {
  delay_8t(us * STM32_CORE_CLOCK / 8);
}
inline void delay_milliseconds(uint32_t ms) {
  delay_8t(ms * 125 * STM32_CORE_CLOCK);
}

void pause_sweep(void);
void resume_sweep(void);
void toggle_sweep(void);
int load_properties(uint32_t id);

#ifdef USE_BACKUP
#endif

void set_sweep_points(uint16_t points);

#ifdef REMOTE_DESKTOP
// State flags for remote touch state

void remote_touch_set(uint16_t state, int16_t x, int16_t y);
void send_region(remote_region_t *rd, uint8_t *buf, uint16_t size);
#endif

#define SWEEP_ENABLE 0x01
#define SWEEP_ONCE 0x02
#define SWEEP_BINARY 0x08
#define SWEEP_REMOTE 0x40
#define SWEEP_UI_MODE 0x80

// Global flag to indicate when calibration is in critical phase to prevent UI flash operations

/*
 * Measure timings for si5351 generator, after ready
 */
// Enable si5351 timing command, used for debug align times
// #define ENABLE_SI5351_TIMINGS
#if defined(NANOVNA_F303)
// Generator ready delays, values in us
#define DELAY_BAND_1_2 US2ST(100)       // Delay for bands 1-2
#define DELAY_BAND_3_4 US2ST(120)       // Delay for bands 3-4
#define DELAY_BANDCHANGE US2ST(2000)    // Band changes need set additional delay after reset PLL
#define DELAY_CHANNEL_CHANGE US2ST(100) // Switch channel delay
#define DELAY_SWEEP_START US2ST(2000)   // Delay at sweep start
// Delay after set new PLL values in ms, and send reset
#define DELAY_RESET_PLL_BEFORE 0   //    0 (0 for disabled)
#define DELAY_RESET_PLL_AFTER 4000 // 4000 (0 for disabled)
#else
// Generator ready delays, values in us
#define DELAY_BAND_1_2 US2ST(100)       // 0 Delay for bands 1-2
#define DELAY_BAND_3_4 US2ST(140)       // 1 Delay for bands 3-4
#define DELAY_BANDCHANGE US2ST(5000)    // 2 Band changes need set additional delay after reset PLL
#define DELAY_CHANNEL_CHANGE US2ST(100) // 3 Switch channel delay
#define DELAY_SWEEP_START US2ST(100)    // 4 Delay at sweep start
// Delay after before/after set new PLL values in ms
#define DELAY_RESET_PLL_BEFORE 0        // 5    0 (0 for disabled)
#define DELAY_RESET_PLL_AFTER 4000      // 6 4000 (0 for disabled)
#endif

/*
 * dsp.c
 */
// Define aic3204 source clock frequency (on 8MHz used fractional multiplier, and possible little
// phase error)
#define AUDIO_CLOCK_REF (8000000U)
// Define aic3204 source clock frequency (on 12288000U used integer multiplier)
// #define AUDIO_CLOCK_REF       (12288000U)
// Disable AIC PLL clock, use input as CODEC_CLKIN (not stable on some devices, on long work)
// #define AUDIO_CLOCK_REF       (98304000U)

// Buffer contain left and right channel samples (need x2)
#define AUDIO_BUFFER_LEN (AUDIO_SAMPLES_COUNT * 2)

// Bandwidth depend from AUDIO_SAMPLES_COUNT and audio ADC frequency
// for AUDIO_SAMPLES_COUNT = 48 and ADC =  48kHz one measure give  48000/48=1000Hz
// for AUDIO_SAMPLES_COUNT = 48 and ADC =  96kHz one measure give  96000/48=2000Hz
// for AUDIO_SAMPLES_COUNT = 48 and ADC = 192kHz one measure give 192000/48=4000Hz
// Define additional measure count for menus
#if AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT == 16000
#define BANDWIDTH_8000 (1 - 1)
#define BANDWIDTH_4000 (2 - 1)
#define BANDWIDTH_1000 (8 - 1)
#define BANDWIDTH_333 (24 - 1)
#define BANDWIDTH_100 (80 - 1)
#define BANDWIDTH_30 (256 - 1)
#elif AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT == 8000
#define BANDWIDTH_8000 (1 - 1)
#define BANDWIDTH_4000 (2 - 1)
#define BANDWIDTH_1000 (8 - 1)
#define BANDWIDTH_333 (24 - 1)
#define BANDWIDTH_100 (80 - 1)
#define BANDWIDTH_30 (256 - 1)
#elif AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT == 4000
#define BANDWIDTH_4000 (1 - 1)
#define BANDWIDTH_2000 (2 - 1)
#define BANDWIDTH_1000 (4 - 1)
#define BANDWIDTH_333 (12 - 1)
#define BANDWIDTH_100 (40 - 1)
#define BANDWIDTH_30 (132 - 1)
#elif AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT == 2000
#define BANDWIDTH_2000 (1 - 1)
#define BANDWIDTH_1000 (2 - 1)
#define BANDWIDTH_333 (6 - 1)
#define BANDWIDTH_100 (20 - 1)
#define BANDWIDTH_30 (66 - 1)
#define BANDWIDTH_10 (200 - 1)
#elif AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT == 1000
#define BANDWIDTH_1000 (1 - 1)
#define BANDWIDTH_333 (3 - 1)
#define BANDWIDTH_100 (10 - 1)
#define BANDWIDTH_30 (33 - 1)
#define BANDWIDTH_10 (100 - 1)
#endif

typedef int16_t audio_sample_t;
void dsp_process(audio_sample_t *src, size_t len);
void reset_dsp_accumerator(void);
void calculate_gamma(float *gamma);
void fetch_amplitude(float *gamma);
void fetch_amplitude_ref(float *gamma);
void generate_dsp_table(int offset);

/*
 * tlv320aic3204.c
 */

void tlv320aic3204_init(void);
void tlv320aic3204_set_gain(uint8_t lgain, uint8_t rgain);
void tlv320aic3204_select(uint8_t channel);
void tlv320aic3204_write_reg(uint8_t page, uint8_t reg, uint8_t data);

// Common utilities
int32_t my_atoi(const char *p);
uint32_t my_atoui(const char *p);
float my_atof(const char *p);
int get_str_index(const char *v, const char *list);
int parse_line(char *line, char *args[], int max_cnt);
void swap_bytes(uint16_t *buf, int size);
int packbits(char *source, char *dest, int size);
void delay_8t(uint32_t cycles);

#include "processing/vna_math.h"

/*
 * plot.c
 */
// LCD display size settings
#ifdef LCD_320X240 // 320x240 display plot definitions
#define LCD_WIDTH 320
#define LCD_HEIGHT 240

// Define maximum distance in pixel for pickup marker (can be bigger for big displays)
#define MARKER_PICKUP_DISTANCE 20
// Used marker image settings
#define _USE_MARKER_SET_ 1
// Used font settings
#define USE_FONT_ID 1
#define USE_SMALL_FONT_ID 0

// Plot area size settings
// Offset of plot area (size of additional info at left side)
#define OFFSETX 10
#define OFFSETY 0

// Grid count, must divide
// #define NGRIDY                     10
#define NGRIDY 8

// Plot area WIDTH better be n*(POINTS_COUNT-1)
#define WIDTH 300
// Plot area HEIGHT = NGRIDY * GRIDY
#define HEIGHT 232

// GRIDX calculated depends from frequency span
// GRIDY depend from HEIGHT and NGRIDY, must be integer
#define GRIDY (HEIGHT / NGRIDY)

// Need for reference marker draw
#define CELLOFFSETX 5
#define AREA_WIDTH_NORMAL (CELLOFFSETX + WIDTH + 1 + 4)
#define AREA_HEIGHT_NORMAL (HEIGHT + 1)

// Smith/polar chart
#define P_CENTER_X (CELLOFFSETX + WIDTH / 2)
#define P_CENTER_Y (HEIGHT / 2)
#define P_RADIUS (HEIGHT / 2)

// Other settings (battery/calibration/frequency text position)
// Battery icon position
#define BATTERY_ICON_POSX 1
#define BATTERY_ICON_POSY 1

// Calibration text coordinates
#define CALIBRATION_INFO_POSX 0
#define CALIBRATION_INFO_POSY 100

#define FREQUENCIES_XPOS1 OFFSETX
#define FREQUENCIES_XPOS2 (LCD_WIDTH - SFONT_STR_WIDTH(23))
#define FREQUENCIES_XPOS3 (LCD_WIDTH / 2 + OFFSETX - SFONT_STR_WIDTH(16) / 2)
#define FREQUENCIES_YPOS (AREA_HEIGHT_NORMAL)
#endif // end 320x240 display plot definitions

#ifdef LCD_480X320 // 480x320 display definitions
#define LCD_WIDTH 480
#define LCD_HEIGHT 320

// Define maximum distance in pixel for pickup marker (can be bigger for big displays)
#define MARKER_PICKUP_DISTANCE 30
// Used marker image settings
#define _USE_MARKER_SET_ 2
// Used font settings
#define USE_FONT_ID 2
#define USE_SMALL_FONT_ID 2

// Plot area size settings
// Offset of plot area (size of additional info at left side)
#define OFFSETX 15
#define OFFSETY 0

// Grid count, must divide
// #define NGRIDY                     10
#define NGRIDY 8

// Plot area WIDTH better be n*(POINTS_COUNT-1)
#define WIDTH 455
// Plot area HEIGHT = NGRIDY * GRIDY
#define HEIGHT 304

// GRIDX calculated depends from frequency span
// GRIDY depend from HEIGHT and NGRIDY, must be integer
#define GRIDY (HEIGHT / NGRIDY)

// Need for reference marker draw
#define CELLOFFSETX 5
#define AREA_WIDTH_NORMAL (CELLOFFSETX + WIDTH + 1 + 4)
#define AREA_HEIGHT_NORMAL (HEIGHT + 1)

// Smith/polar chart
#define P_CENTER_X (CELLOFFSETX + WIDTH / 2)
#define P_CENTER_Y (HEIGHT / 2)
#define P_RADIUS (HEIGHT / 2)

// Other settings (battery/calibration/frequency text position)
// Battery icon position
#define BATTERY_ICON_POSX 3
#define BATTERY_ICON_POSY 2

// Calibration text coordinates
#define CALIBRATION_INFO_POSX 0
#define CALIBRATION_INFO_POSY 100

#define FREQUENCIES_XPOS1 OFFSETX
#define FREQUENCIES_XPOS2 (LCD_WIDTH - SFONT_STR_WIDTH(22))
#define FREQUENCIES_XPOS3 (LCD_WIDTH / 2 + OFFSETX - SFONT_STR_WIDTH(14) / 2)
#define FREQUENCIES_YPOS (AREA_HEIGHT_NORMAL + 2)
#endif // end 480x320 display plot definitions

// UI size defines
// Text offset in menu
#define MENU_TEXT_OFFSET 6
#define MENU_ICON_OFFSET 4
// Scale / ref quick touch position
#define UI_SCALE_REF_X0 (OFFSETX - 5)
#define UI_SCALE_REF_X1 (OFFSETX + CELLOFFSETX + 10)
// Leveler Marker mode select
#define UI_MARKER_Y0 30
// Maximum menu buttons count
#define MENU_BUTTON_MAX 16
#define MENU_BUTTON_MIN 8
// Menu buttons y offset
#define MENU_BUTTON_Y_OFFSET 1
// Menu buttons size = 21 for icon and 10 chars
#define MENU_BUTTON_WIDTH (7 + FONT_STR_WIDTH(12))
#define MENU_BUTTON_HEIGHT(n) (AREA_HEIGHT_NORMAL / (n))
#define MENU_BUTTON_BORDER 1
#define KEYBOARD_BUTTON_BORDER 1
#define BROWSER_BUTTON_BORDER 1
// Browser window settings
#define FILES_COLUMNS (LCD_WIDTH / 160)             // columns in browser
#define FILES_ROWS 10                               // rows in browser
#define FILES_PER_PAGE (FILES_COLUMNS * FILES_ROWS) // FILES_ROWS * FILES_COLUMNS
#define FILE_BOTTOM_HEIGHT 20                       // Height of bottom buttons (< > X)
#define FILE_BUTTON_HEIGHT                                                                         \
  ((LCD_HEIGHT - FILE_BOTTOM_HEIGHT) / FILES_ROWS) // Height of file buttons

// Define message box width
#define MESSAGE_BOX_WIDTH 180

// Height of numerical input field (at bottom)
#define NUM_INPUT_HEIGHT 32
// On screen keyboard button size
#if 1                                                   // Full screen keyboard
#define KP_WIDTH (LCD_WIDTH / 4)                        // numeric keypad button width
#define KP_HEIGHT ((LCD_HEIGHT - NUM_INPUT_HEIGHT) / 4) // numeric keypad button height
#define KP_X_OFFSET 0                                   // numeric keypad X offset
#define KP_Y_OFFSET 0                                   // numeric keypad Y offset
#define KPF_WIDTH (LCD_WIDTH / 10)                      // text keypad button width
#define KPF_HEIGHT KPF_WIDTH                            // text keypad button height
#define KPF_X_OFFSET 0                                  // text keypad X offset
#define KPF_Y_OFFSET (LCD_HEIGHT - NUM_INPUT_HEIGHT - 4 * KPF_HEIGHT) // text keypad Y offset
#else                                                                 // 64 pixel size keyboard
#define KP_WIDTH 64                                                   // numeric keypad button width
#define KP_HEIGHT 64 // numeric keypad button height
#define KP_X_OFFSET (LCD_WIDTH - MENU_BUTTON_WIDTH - 16 - KP_WIDTH * 4) // numeric keypad X offset
#define KP_Y_OFFSET 20                                                  // numeric keypad Y offset
#define KPF_WIDTH (LCD_WIDTH / 10)                                      // text keypad button width
#define KPF_HEIGHT KPF_WIDTH                                            // text keypad button height
#define KPF_X_OFFSET 0                                                  // text keypad X offset
#define KPF_Y_OFFSET (LCD_HEIGHT - NUM_INPUT_HEIGHT - 4 * KPF_HEIGHT)   // text keypad Y offset
#endif

#if USE_FONT_ID == 0
extern const uint8_t X5X7_BITS[];
#define FONT_START_CHAR 0x16
#define FONT_WIDTH 5
#define FONT_GET_HEIGHT 7
#define FONT_STR_WIDTH(n) ((n) * FONT_WIDTH)
#define FONT_STR_HEIGHT 8
#define FONT_GET_DATA(ch) (&X5X7_BITS[((ch) - FONT_START_CHAR) * FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch) (8 - (X5X7_BITS[((ch) - FONT_START_CHAR) * FONT_GET_HEIGHT] & 0x7))

#elif USE_FONT_ID == 1
extern const uint8_t X6X10_BITS[];
#define FONT_START_CHAR 0x16
#define FONT_WIDTH 6
#define FONT_GET_HEIGHT 10
#define FONT_STR_WIDTH(n) ((n) * FONT_WIDTH)
#define FONT_STR_HEIGHT 11
#define FONT_GET_DATA(ch) (&X6X10_BITS[(ch - FONT_START_CHAR) * FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch) (8 - (X6X10_BITS[(ch - FONT_START_CHAR) * FONT_GET_HEIGHT] & 0x7))

#elif USE_FONT_ID == 2
extern const uint8_t X7X11B_BITS[];
#define FONT_START_CHAR 0x16
#define FONT_WIDTH 7
#define FONT_GET_HEIGHT 11
#define FONT_STR_WIDTH(n) ((n) * FONT_WIDTH)
#define FONT_STR_HEIGHT 11
#define FONT_GET_DATA(ch) (&X7X11B_BITS[(ch - FONT_START_CHAR) * FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch) (8 - (X7X11B_BITS[(ch - FONT_START_CHAR) * FONT_GET_HEIGHT] & 7))

#elif USE_FONT_ID == 3
extern const uint8_t X11X14_BITS[];
#define FONT_START_CHAR 0x16
#define FONT_WIDTH 11
#define FONT_GET_HEIGHT 14
#define FONT_STR_WIDTH(n) ((n) * FONT_WIDTH)
#define FONT_STR_HEIGHT 16
#define FONT_GET_DATA(ch) (&X11X14_BITS[(ch - FONT_START_CHAR) * 2 * FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch)                                                                         \
  (14 - (X11X14_BITS[(ch - FONT_START_CHAR) * 2 * FONT_GET_HEIGHT + 1] & 0x7))
#endif

#if USE_SMALL_FONT_ID == 0
extern const uint8_t X5X7_BITS[];
#define SFONT_START_CHAR 0x16
#define SFONT_WIDTH 5
#define SFONT_GET_HEIGHT 7
#define SFONT_STR_WIDTH(n) ((n) * SFONT_WIDTH)
#define SFONT_STR_HEIGHT 8
#define SFONT_GET_DATA(ch) (&X5X7_BITS[((ch) - SFONT_START_CHAR) * SFONT_GET_HEIGHT])
#define SFONT_GET_WIDTH(ch) (8 - (X5X7_BITS[((ch) - SFONT_START_CHAR) * SFONT_GET_HEIGHT] & 0x7))

#elif USE_SMALL_FONT_ID == 1
extern const uint8_t X6X10_BITS[];
#define SFONT_START_CHAR 0x16
#define SFONT_WIDTH 6
#define SFONT_GET_HEIGHT 10
#define SFONT_STR_WIDTH(n) ((n) * SFONT_WIDTH)
#define SFONT_STR_HEIGHT 11
#define SFONT_GET_DATA(ch) (&X6X10_BITS[(ch - SFONT_START_CHAR) * SFONT_GET_HEIGHT])
#define SFONT_GET_WIDTH(ch) (8 - (X6X10_BITS[(ch - SFONT_START_CHAR) * SFONT_GET_HEIGHT] & 0x7))

#elif USE_SMALL_FONT_ID == 2
extern const uint8_t X7X11B_BITS[];
#define SFONT_START_CHAR 0x16
#define SFONT_WIDTH 7
#define SFONT_GET_HEIGHT 11
#define SFONT_STR_WIDTH(n) ((n) * SFONT_WIDTH)
#define SFONT_STR_HEIGHT 11
#define SFONT_GET_DATA(ch) (&X7X11B_BITS[(ch - SFONT_START_CHAR) * SFONT_GET_HEIGHT])
#define SFONT_GET_WIDTH(ch) (8 - (X7X11B_BITS[(ch - SFONT_START_CHAR) * SFONT_GET_HEIGHT] & 7))

#elif USE_SMALL_FONT_ID == 3
extern const uint8_t X11X14_BITS[];
#define SFONT_START_CHAR 0x16
#define SFONT_WIDTH 11
#define SFONT_GET_HEIGHT 14
#define SFONT_STR_WIDTH(n) ((n) * SFONT_WIDTH)
#define SFONT_STR_HEIGHT 16
#define SFONT_GET_DATA(ch) (&X11X14_BITS[(ch - SFONT_START_CHAR) * 2 * SFONT_GET_HEIGHT])
#define SFONT_GET_WIDTH(ch)                                                                        \
  (14 - (X11X14_BITS[(ch - SFONT_START_CHAR) * 2 * SFONT_GET_HEIGHT + 1] & 0x7))
#endif

// Font type defines
enum { FONT_SMALL = 0, FONT_NORMAL };
#if USE_FONT_ID != USE_SMALL_FONT_ID
void lcd_set_font(int type);
#define LCD_SET_FONT(type) lcd_set_font(type)
#else
#define LCD_SET_FONT(type) (void)(type)
#endif

extern const uint8_t NUMFONT16X22[];
#define NUM_FONT_GET_WIDTH 16
#define NUM_FONT_GET_HEIGHT 22
#define NUM_FONT_GET_DATA(ch) (&NUMFONT16X22[(ch) * 2 * NUM_FONT_GET_HEIGHT])

// Glyph names from NUMFONT16X22.c
enum {
  KP_0 = 0,
  KP_1,
  KP_2,
  KP_3,
  KP_4,
  KP_5,
  KP_6,
  KP_7,
  KP_8,
  KP_9,
  KP_PERIOD,
  KP_MINUS,
  KP_BS,
  KP_k,
  KP_M,
  KP_G,
  KP_m,
  KP_u,
  KP_n,
  KP_p,
  KP_X1,
  KP_ENTER,
  KP_PERCENT, // Enter values
#if 0
  KP_INF,
  KP_DB,
  KP_PLUSMINUS,
  KP_KEYPAD,
  KP_SPACE,
  KP_PLUS,
#endif
  // Special uint8_t buttons
  KP_EMPTY = 255 // Empty button
};

/*
 * LC match text output settings
 */
#ifdef VNA_MEASURE_MODULE
// X and Y offset to L/C match text
#define STR_MEASURE_X (OFFSETX + 0)
// Better be aligned by cell (cell height = 32)
#define STR_MEASURE_Y (OFFSETY + 80)
// 1/3 Width of text (use 3 column for data)
#define STR_MEASURE_WIDTH (FONT_WIDTH * 10)
// String Height (need 2 + 0..4 string)
#define STR_MEASURE_HEIGHT (FONT_STR_HEIGHT + 1)
#endif

#ifdef USE_GRID_VALUES
#define GRID_X_TEXT (WIDTH - SFONT_STR_WIDTH(5))
#endif

// Render control chars
#define R_BGCOLOR "\x01" // hex 0x01 set background color
#define R_FGCOLOR "\x02" // hex 0x02 set foreground color
// Set BG / FG color string macros
#define SET_BGCOLOR(idx) R_BGCOLOR #idx
#define SET_FGCOLOR(idx) R_FGCOLOR #idx

#define R_TEXT_COLOR SET_FGCOLOR(\x01) // set  1 color index as foreground
#define R_LINK_COLOR SET_FGCOLOR(\x19) // set 25 color index as foreground

// Additional chars in fonts
#define S_ENTER "\x16"    // hex 0x16
#define S_DELTA "\x17"    // hex 0x17
#define S_SARROW "\x18"   // hex 0x18
#define S_INFINITY "\x19" // hex 0x19
#define S_LARROW "\x1A"   // hex 0x1A
#define S_RARROW "\x1B"   // hex 0x1B
#define S_PI "\x1C"       // hex 0x1C
#define S_MICRO "\x1D"    // hex 0x1D
#define S_OHM "\x1E"      // hex 0x1E
#define S_DEGREE "\x1F"   // hex 0x1F
#define S_SIEMENS "S"     //
#define S_DB "dB"         //
#define S_HZ "Hz"         //
#define S_FARAD "F"       //
#define S_HENRY "H"       //
#define S_SECOND "s"      //
#define S_METRE "m"       //
#define S_VOLT "V"        //
#define S_AMPER "A"       //
#define S_PPM "ppm"       //

// Max palette indexes in config

void set_trace_type(int t, int type, int channel);
void set_trace_channel(int t, int channel);
void set_trace_scale(int t, float scale);
void set_active_trace(int t);
void set_trace_refpos(int t, float refpos);
float get_trace_scale(int t);
float get_trace_refpos(int t);
void set_trace_enable(int t, bool enable);
const char *get_trace_chname(int t);

void set_electrical_delay(int ch, float seconds);
float get_electrical_delay(void);
void set_s21_offset(float offset);
// Port declarations
// Forward declarations to avoid circular dependencies
struct processing_port;
struct ui_module_port;
struct usb_command_server_port;

extern const struct processing_port PROCESSING_PORT;
extern const struct ui_module_port UI_PORT;
extern const struct usb_command_server_port USB_PORT;

extern const char *const INFO_ABOUT[];

extern properties_t current_props;
extern config_t config;

float groupdelay_from_array(int i, const float *v);

// Runtime control functions exposed for shell
void pause_sweep(void);
void resume_sweep(void);
void toggle_sweep(void);
void set_power(uint8_t value);
void set_bandwidth(uint16_t bw_count);
uint32_t get_bandwidth_frequency(uint16_t bw_freq);
void set_sweep_points(uint16_t points);
void set_sweep_frequency(uint16_t type, freq_t freq);
void set_sweep_frequency_internal(uint16_t type, freq_t freq, bool enforce_order); // Exposed
void reset_sweep_frequency(void);
void app_measurement_update_frequencies(void);
bool need_interpolate(freq_t start, freq_t stop, uint16_t points);
void sweep_get_ordered(freq_t *start, freq_t *stop);
int load_properties(uint32_t id);
void set_marker_index(int m, int idx);

// Stat helper
struct stat_t {
  int16_t rms[2];
  int16_t ave[2];
};

void plot_init(void);
void update_grid(freq_t fstart, freq_t fstop);
void request_to_redraw(uint16_t mask);
void request_to_draw_cells_behind_menu(void);
void request_to_draw_cells_behind_numeric_input(void);
void request_to_draw_marker(uint16_t idx);
void redraw_marker(int8_t marker);
void draw_all(void);
void set_area_size(uint16_t w, uint16_t h);
void plot_set_measure_mode(uint8_t mode);
uint16_t plot_get_measure_channels(void);

int distance_to_index(int8_t t, uint16_t idx, int16_t x, int16_t y);
int search_nearest_index(int x, int y, int t);

void toggle_stored_trace(int idx);
uint8_t get_stored_traces(void);

const char *get_trace_typename(int t, int marker_smith_format);
const char *get_smith_format_names(int m);

// Marker search functions
#define MK_SEARCH_LEFT (-1)
#define MK_SEARCH_RIGHT 1
void marker_search(void);
void marker_search_dir(int16_t from, int16_t dir);

// Plot Redraw flags.
#define REDRAW_PLOT (1 << 0)      // Update all trace indexes in plot area
#define REDRAW_AREA (1 << 1)      // Redraw all plot area
#define REDRAW_CELLS (1 << 2)     // Redraw only updated cells
#define REDRAW_FREQUENCY (1 << 3) // Redraw Start/Stop/Center/Span frequency, points count, Avg/IFBW
#define REDRAW_CAL_STATUS (1 << 4) // Redraw calibration status (left screen part)
#define REDRAW_MARKER (1 << 5)     // Redraw marker plates and text
#define REDRAW_REFERENCE (1 << 6)  // Redraw reference
#define REDRAW_GRID_VALUE (1 << 7) // Redraw grid values
#define REDRAW_BATTERY (1 << 8)    // Redraw battery state
#define REDRAW_CLRSCR (1 << 9)     // Clear all screen before redraw
#define REDRAW_BACKUP (1 << 10)    // Update backup information

// Set this if need update all screen
#define REDRAW_ALL                                                                                 \
  (REDRAW_CLRSCR | REDRAW_AREA | REDRAW_CAL_STATUS | REDRAW_BATTERY | REDRAW_FREQUENCY)

/*
 * ili9341.c
 */

typedef struct {
  uint8_t transparent : 1;
  int8_t shift_x : 7;
  int8_t shift_y : 8;
} vector_data_t;

// Used for easy define big Bitmap as 0bXXXXXXXXX image
#define _BMP8(d) ((d) & 0xFF)
#define _BMP16(d) (((d) >> 8) & 0xFF), ((d) & 0xFF)
#define _BMP24(d) (((d) >> 16) & 0xFF), (((d) >> 8) & 0xFF), ((d) & 0xFF)
#define _BMP32(d) (((d) >> 24) & 0xFF), (((d) >> 16) & 0xFF), (((d) >> 8) & 0xFF), ((d) & 0xFF)

void lcd_init(void);
void lcd_bulk(int x, int y, int w, int h);
void lcd_fill(int x, int y, int w, int h);

#if DISPLAY_CELL_BUFFER_COUNT == 1
#define lcd_get_cell_buffer() spi_buffer
#define lcd_bulk_continue lcd_bulk
#define lcd_bulk_finish()                                                                          \
  {}
#else
pixel_t *lcd_get_cell_buffer(void); // get buffer for cell render
void lcd_bulk_continue(int x, int y, int w,
                       int h); // send data to display, in DMA mode use it, no wait DMA complete
void lcd_bulk_finish(void);    // wait DMA complete (need call at end)
#endif

void lcd_set_foreground(uint16_t fg_idx);
void lcd_set_background(uint16_t bg_idx);
void lcd_set_colors(uint16_t fg_idx, uint16_t bg_idx);
void lcd_clear_screen(void);
void lcd_blit_bitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     const uint8_t *bitmap);
void lcd_blit_bitmap_scale(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t size,
                           const uint8_t *b);
void lcd_drawchar(uint8_t ch, int x, int y);
#if 0
void lcd_drawstring(int16_t x, int16_t y, const char *str);
#else
// use printf for draw string
#define LCD_DRAWSTRING lcd_printf
#endif
int lcd_printf(int16_t x, int16_t y, const char *fmt, ...);
int lcd_printf_v(int16_t x, int16_t y, const char *fmt, ...);
int lcd_printf_va(int16_t x, int16_t y, const char *fmt, va_list ap);
int lcd_drawchar_size(uint8_t ch, int x, int y, uint8_t size);
void lcd_drawstring_size(const char *str, int x, int y, uint8_t size);
void lcd_drawfont(uint8_t ch, int x, int y);
void lcd_read_memory(int x, int y, int w, int h, uint16_t *out);
void lcd_line(int x0, int y0, int x1, int y1);
void lcd_vector_draw(int x, int y, const vector_data_t *v);

uint32_t lcd_send_register(uint8_t cmd, uint8_t len, const uint8_t *data);
void lcd_set_flip(bool flip);

#ifdef USE_SD_CARD
#include "ff.h"
#include "diskio.h"

#if SPI_BUFFER_SIZE < 1024
#error "SPI_BUFFER_SIZE for SD card support need size >= 1024"
#endif

FATFS *filesystem_volume(void);
FIL *filesystem_file(void);
void test_log(void);
#endif

/*
 * flash.c
 */
#define CONFIG_MAGIC 0x434f4e56 // Config magic value (allow reset on new config version)
#define PROPERTIES_MAGIC                                                                           \
  0x434f4e54 // Properties magic value (allow reset on new properties version)

#define NO_SAVE_SLOT ((uint16_t)(-1))

#define frequency0 current_props._frequency0
#define frequency1 current_props._frequency1
#define cal_frequency0 current_props._cal_frequency0
#define cal_frequency1 current_props._cal_frequency1
#define var_freq current_props._var_freq
#define sweep_points current_props._sweep_points
#define cal_sweep_points current_props._cal_sweep_points
#define cal_power current_props._cal_power
#define cal_status current_props._cal_status
#define cal_data current_props._cal_data
#define electrical_delay_s11 current_props._electrical_delay[0]
#define electrical_delay_s21 current_props._electrical_delay[1]
#define s21_offset current_props._s21_offset
#define velocity_factor current_props._velocity_factor
#define trace current_props._trace
#define current_trace current_props._current_trace
#define markers current_props._markers
#define active_marker current_props._active_marker
#define previous_marker current_props._previous_marker
#ifdef VNA_Z_RENORMALIZATION
#define cal_load_r current_props._cal_load_r
#else
#define CAL_LOAD_R 50.0f
#endif

#define props_mode current_props._mode
#define DOMAIN_WINDOW (props_mode & TD_WINDOW)
#define DOMAIN_FUNC (props_mode & TD_FUNC)

#define FREQ_STARTSTOP()                                                                           \
  { props_mode &= ~TD_CENTER_SPAN; }
#define FREQ_CENTERSPAN()                                                                          \
  { props_mode |= TD_CENTER_SPAN; }
#define FREQ_IS_STARTSTOP() (!(props_mode & TD_CENTER_SPAN))
#define FREQ_IS_CENTERSPAN() (props_mode & TD_CENTER_SPAN)
#define FREQ_IS_CW() (frequency0 == frequency1)

#define GET_TRACE_SCALE(t) current_props._trace[t].scale
#define GET_TRACE_REFPOS(t) current_props._trace[t].refpos

#define VNA_MODE(idx) (config._vna_mode & (1 << (idx)))
#define lever_mode config._lever_mode
#define IF_OFFSET config.IF_freq

#ifdef USE_VARIABLE_OFFSET
#define IF_OFFSET_MIN ((int32_t)FREQUENCY_OFFSET_STEP)
#define IF_OFFSET_MAX ((int32_t)(AUDIO_ADC_FREQ / 2))
static inline int32_t clamp_if_offset(int32_t offset) {
  if (offset < IF_OFFSET_MIN) {
    offset = IF_OFFSET_MIN;
  } else if (offset > IF_OFFSET_MAX) {
    offset = IF_OFFSET_MAX;
  }
  return offset;
}
#else
static inline int32_t clamp_if_offset(int32_t offset) {
  (void)offset;
  return FREQUENCY_OFFSET;
}
#endif

static inline uint32_t clamp_harmonic_threshold(uint32_t value) {
  if (value < FREQUENCY_MIN) {
    value = FREQUENCY_MIN;
  } else if (value > FREQUENCY_MAX) {
    value = FREQUENCY_MAX;
  }
  return value;
}
#ifdef USE_DIGIT_SEPARATOR
#define DIGIT_SEPARATOR (VNA_MODE(VNA_MODE_SEPARATOR) ? ',' : '.')
#else
#define DIGIT_SEPARATOR '.'
#endif

static inline freq_t get_sweep_frequency(uint16_t type) {
  freq_t start = frequency0;
  freq_t stop = frequency1;
  if (start > stop) {
    freq_t tmp = start;
    start = stop;
    stop = tmp;
  }
  switch (type) {
  case ST_START:
    return start;
  case ST_STOP:
    return stop;
  case ST_CENTER:
    return (freq_t)(((uint64_t)start + (uint64_t)stop) >> 1);
  case ST_SPAN:
    return stop - start;
  case ST_CW:
    return frequency0;
  }
  return 0;
}

int caldata_save(uint32_t id);
int caldata_recall(uint32_t id);
const properties_t *get_properties(uint32_t id);

int config_save(void);
int config_recall(void);

void clear_all_config_prop_data(void);

/*
 * ui.c
 */
// Enter in leveler search mode after search click
// #define UI_USE_LEVELER_SEARCH_MODE

void ui_init(void);
void ui_process(void);
void ui_attach_event_bus(event_bus_t *bus);

void handle_touch_interrupt(void);

void ui_touch_cal_exec(void);
void ui_touch_draw_test(void);
void ui_enter_dfu(void);

void ui_message_box(const char *header, const char *text, uint32_t delay);
#ifdef USE_SD_CARD
bool sd_card_load_config(void);
#endif

#define TOUCH_THRESHOLD 2000
/*
 * misclinous
 */
int plot_printf(char *str, int, const char *fmt, ...);
#define PULSE                                                                                      \
  do {                                                                                             \
    palClearPad(GPIOC, GPIOC_LED);                                                                 \
    palSetPad(GPIOC, GPIOC_LED);                                                                   \
  } while (0)

#define ARRAY_COUNT(a) (sizeof(a) / sizeof(*(a)))
// Speed profile definition
#define START_PROFILE systime_t time = chVTGetSystemTimeX();
#define STOP_PROFILE                                                                               \
  { lcd_printf_v(1, 1, "T:%08d", chVTGetSystemTimeX() - time); }
// Macros for convert define value to string
#define STR1(x) #x
#define DEFINE_TO_STR(x) STR1(x)
#define SWAP(type, x, y)                                                                           \
  {                                                                                                \
    type t = x;                                                                                    \
    (x) = y;                                                                                       \
    (y) = t;                                                                                       \
  }
/*EOF*/
