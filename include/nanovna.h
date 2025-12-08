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
#include "ch.h"
#include "infra/event/event_bus.h"
#include <stdalign.h>
#include <stdarg.h>
#include <stdint.h>

// Include granular headers
#include "vna_config.h"
#include "vna_constants.h"
#include "vna_types.h"

// Hardware depend functions declaration
#include "platform/boards/stm32_peripherals.h"
#include "platform/hal.h"

/*
 * Global State Externs
 */
extern alignas(4) float measured[2][SWEEP_POINTS_MAX][2];
extern config_t config;
extern properties_t current_props;

extern  uint8_t sweep_mode;
extern const char* const info_about[];

// Global flag to indicate when calibration is in critical phase to prevent UI flash operations
extern volatile bool calibration_in_progress;

extern pixel_t foreground_color;
extern pixel_t background_color;
extern alignas(4) pixel_t spi_buffer[SPI_BUFFER_SIZE];

extern uint16_t lastsaveid;

/*
 * Function Prototypes
 */

/* calibration.c (or similar) */
void cal_collect(uint16_t type);
void cal_done(void);
int caldata_save(uint32_t id);
int caldata_recall(uint32_t id);
const properties_t *get_properties(uint32_t id);
void clear_all_config_prop_data(void);

/* frequency/sweep */
freq_t get_frequency(uint16_t idx);
freq_t get_frequency_step(void);
void   set_marker_index(int m, int idx);
freq_t get_marker_frequency(int marker);
void   reset_sweep_frequency(void);
void   set_sweep_frequency(uint16_t type, freq_t frequency);
void set_bandwidth(uint16_t bw_count);
uint32_t get_bandwidth_frequency(uint16_t bw_freq);
void set_power(uint8_t value);
void set_sweep_points(uint16_t points);
void pause_sweep(void);
void resume_sweep(void);
void toggle_sweep(void);

/* config/misc */
void    set_smooth_factor(uint8_t factor);
uint8_t get_smooth_factor(void);
int  load_properties(uint32_t id);
int config_save(void);
int config_recall(void);

/* utils */
int32_t  my_atoi(const char *p);
uint32_t my_atoui(const char *p);
float    my_atof(const char *p);
bool strcmpi(const char *t1, const char *t2);
int get_str_index(const char *v, const char *list);
int parse_line(char *line, char* args[], int max_cnt);
void swap_bytes(uint16_t *buf, int size);
int packbits(char *source, char *dest, int size);
void _delay_8t(uint32_t cycles);
inline void delay_microseconds(uint32_t us) {_delay_8t(us*STM32_CORE_CLOCK/8);}
inline void delay_milliseconds(uint32_t ms) {_delay_8t(ms*125*STM32_CORE_CLOCK);}

/* remote desktop */
#ifdef __REMOTE_DESKTOP__
void remote_touch_set(uint16_t state, int16_t x, int16_t y);
void send_region(remote_region_t *rd, uint8_t * buf, uint16_t size);
#endif

/* dsp */
void dsp_process(audio_sample_t *src, size_t len);
void reset_dsp_accumerator(void);
void calculate_gamma(float *gamma);
void fetch_amplitude(float *gamma);
void fetch_amplitude_ref(float *gamma);
void generate_dsp_table(int offset);

/* tlv320aic3204 */
void tlv320aic3204_init(void);
void tlv320aic3204_set_gain(uint8_t lgain, uint8_t rgain);
void tlv320aic3204_select(uint8_t channel);
void tlv320aic3204_write_reg(uint8_t page, uint8_t reg, uint8_t data);

/* vna_math */
#include "processing/vna_math.h"

/* trace/marker settings */
extern const trace_info_t trace_info_list[MAX_TRACE_TYPE];
extern const marker_info_t marker_info_list[MS_END];

void set_trace_type(int t, int type, int channel);
void set_trace_channel(int t, int channel);
void set_trace_scale(int t, float scale);
void set_active_trace(int t);
void set_trace_refpos(int t, float refpos);
void set_trace_enable(int t, bool enable);
const char *get_trace_chname(int t);
void  set_electrical_delay(int ch, float seconds);
float get_electrical_delay(void);
void set_s21_offset(float offset);
float groupdelay_from_array(int i, const float *v);
const char *get_trace_typename(int t, int marker_smith_format);
const char *get_smith_format_names(int m);

/* plot */
void apply_vna_mode(uint16_t idx, vna_mode_ops operation);
void plot_init(void);
void update_grid(freq_t fstart, freq_t fstop);
void request_to_redraw(uint32_t mask);
void force_set_markmap(void);
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
void marker_search(void);
void marker_search_dir(int16_t from, int16_t dir);

/* lcd */
void lcd_init(void);
void lcd_bulk(int x, int y, int w, int h);
void lcd_fill(int x, int y, int w, int h);
#if DISPLAY_CELL_BUFFER_COUNT == 1
#define lcd_get_cell_buffer()             spi_buffer
#define lcd_bulk_continue                 lcd_bulk
#define lcd_bulk_finish()                 {}
#else
pixel_t *lcd_get_cell_buffer(void);
void lcd_bulk_continue(int x, int y, int w, int h);
void lcd_bulk_finish(void);
#endif
void lcd_set_foreground(uint16_t fg_idx);
void lcd_set_background(uint16_t bg_idx);
void lcd_set_colors(uint16_t fg_idx, uint16_t bg_idx);
void lcd_clear_screen(void);
void lcd_blit_bitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *bitmap);
void lcd_blit_bitmap_scale(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t size, const uint8_t *b);
void lcd_drawchar(uint8_t ch, int x, int y);
#define lcd_drawstring lcd_printf
int  lcd_printf(int16_t x, int16_t y, const char *fmt, ...);
int  lcd_printf_v(int16_t x, int16_t y, const char *fmt, ...);
int  lcd_printf_va(int16_t x, int16_t y, const char *fmt, va_list ap);
int  lcd_drawchar_size(uint8_t ch, int x, int y, uint8_t size);
void lcd_drawstring_size(const char *str, int x, int y, uint8_t size);
void lcd_drawfont(uint8_t ch, int x, int y);
void lcd_read_memory(int x, int y, int w, int h, uint16_t* out);
void lcd_line(int x0, int y0, int x1, int y1);
void lcd_vector_draw(int x, int y, const vector_data *v);
uint32_t lcd_send_register(uint8_t cmd, uint8_t len, const uint8_t *data);
void     lcd_set_flip(bool flip);

/* sd card */
#ifdef __USE_SD_CARD__
#include "ff.h"
#include "diskio.h"
FATFS *filesystem_volume(void);
FIL   *filesystem_file(void);
void test_log(void);
bool sd_card_load_config(void);
#endif

/* ui */
void ui_init(void);
void ui_process(void);
void ui_attach_event_bus(event_bus_t* bus);
void handle_touch_interrupt(void);
void ui_touch_cal_exec(void);
void ui_touch_draw_test(void);
void ui_enter_dfu(void);
void ui_message_box(const char *header, const char *text, uint32_t delay);

/* misc */
int plot_printf(char *str, int, const char *fmt, ...);
#define PULSE do { palClearPad(GPIOC, GPIOC_LED); palSetPad(GPIOC, GPIOC_LED);} while(0)
#define ARRAY_COUNT(a)    (sizeof(a)/sizeof(*(a)))
#define START_PROFILE   systime_t time = chVTGetSystemTimeX();
#define STOP_PROFILE    {lcd_printf_v(1, 1, "T:%08d", chVTGetSystemTimeX() - time);}
#define STR1(x)  #x
#define define_to_STR(x)  STR1(x)
#define SWAP(type, x, y) {type t = x; x=y; y=t;}

/*
 * Accessor Macros (Logic)
 */
#define frequency0          current_props._frequency0
#define frequency1          current_props._frequency1
#define cal_frequency0      current_props._cal_frequency0
#define cal_frequency1      current_props._cal_frequency1
#define var_freq            current_props._var_freq
#define sweep_points        current_props._sweep_points
#define cal_sweep_points    current_props._cal_sweep_points
#define cal_power           current_props._cal_power
#define cal_status          current_props._cal_status
#define cal_data            current_props._cal_data
#define electrical_delayS11 current_props._electrical_delay[0]
#define electrical_delayS21 current_props._electrical_delay[1]
#define s21_offset          current_props._s21_offset
#define velocity_factor     current_props._velocity_factor
#define trace               current_props._trace
#define current_trace       current_props._current_trace
#define markers             current_props._markers
#define active_marker       current_props._active_marker
#define previous_marker     current_props._previous_marker
#ifdef __VNA_Z_RENORMALIZATION__
 #define cal_load_r         current_props._cal_load_r
#else
 #define cal_load_r         50.0f
#endif

#define props_mode          current_props._mode
#define domain_window      (props_mode&TD_WINDOW)
#define domain_func        (props_mode&TD_FUNC)

#define FREQ_STARTSTOP()       {props_mode&=~TD_CENTER_SPAN;}
#define FREQ_CENTERSPAN()      {props_mode|= TD_CENTER_SPAN;}
#define FREQ_IS_STARTSTOP()  (!(props_mode&TD_CENTER_SPAN))
#define FREQ_IS_CENTERSPAN()   (props_mode&TD_CENTER_SPAN)
#define FREQ_IS_CW()           (frequency0 == frequency1)

#define get_trace_scale(t)      current_props._trace[t].scale
#define get_trace_refpos(t)     current_props._trace[t].refpos

#define VNA_MODE(idx)        (config._vna_mode&(1<<idx))
#define GET_PALTETTE_COLOR(idx)  config._lcd_palette[idx]
#define lever_mode           config._lever_mode
#define IF_OFFSET            config._IF_freq

#ifdef USE_VARIABLE_OFFSET
#define IF_OFFSET_MIN        ((int32_t)FREQUENCY_OFFSET_STEP)
#define IF_OFFSET_MAX        ((int32_t)(AUDIO_ADC_FREQ / 2))
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
#ifdef __DIGIT_SEPARATOR__
#define DIGIT_SEPARATOR      (VNA_MODE(VNA_MODE_SEPARATOR) ? ',' : '.')
#else
#define DIGIT_SEPARATOR      '.'
#endif

static inline freq_t
get_sweep_frequency(uint16_t type)
{
  freq_t start = frequency0;
  freq_t stop  = frequency1;
  if (start > stop) {
    freq_t tmp = start;
    start = stop;
    stop  = tmp;
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
