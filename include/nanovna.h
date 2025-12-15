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

#include "rf/sweep_core.h"
#include "processing/vna_math.h"
#include "core/globals.h"
#include "ui/ui_style.h"
#include "ui/core/ui_core.h"
#include "processing/calibration.h"

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



/*
 * plot.c
 */
#include "ui/ui_config.h"

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

// Common utilities
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
