/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
 * Based on TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
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

#include "app/app_features.h"
#include "app/application.h"
#include "app/sweep_service.h"

#include "ch.h"
#include "hal.h"
#include "hal_i2c_lld.h"
#include "si5351.h"
#include "nanovna.h"
#include "app/shell.h"
#include "usbcfg.h"
#include "platform/hal.h"
#include "services/config_service.h"
#include "services/event_bus.h"
#include "version_info.h"
#include "measurement/pipeline.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 *  Shell settings
 */
// If need run shell as thread (use more amount of memory for stack), after
// enable this need reduce spi_buffer size, by default shell runs in main thread
// #define VNA_SHELL_THREAD

static event_bus_t app_event_bus;
static event_bus_subscription_t app_event_slots[8];

#define APP_EVENT_QUEUE_DEPTH 8U
static msg_t app_event_queue_storage[APP_EVENT_QUEUE_DEPTH];
static event_bus_queue_node_t app_event_nodes[APP_EVENT_QUEUE_DEPTH];

static measurement_pipeline_t measurement_pipeline;

// SD card access was removed; configuration persists using internal flash only.

// Shell frequency printf format
// #define VNA_FREQ_FMT_STR         "%lu"
#define VNA_FREQ_FMT_STR "%u"

#define VNA_SHELL_FUNCTION(command_name) static void command_name(int argc, char* argv[])

// Shell command line buffer, args, nargs, and function ptr
static char shell_line[VNA_SHELL_MAX_LENGTH];

#if ENABLED_DUMP_COMMAND
static void cmd_dump(int argc, char* argv[]);
#endif

// Optional commands are controlled through app_features.h profiles.

uint8_t sweep_mode = SWEEP_ENABLE | SWEEP_ONCE;
// Sweep measured data
float measured[2][SWEEP_POINTS_MAX][2];

// Version text, displayed in Config->Version menu, also send by info command
const char* info_about[] = {
    "Board: " BOARD_NAME, "NanoVNA-X maintainer: @momentics <momentics@gmail.com>",
    "Refactored from @DiSlord and @edy555", "Licensed under GPL.",
    "  https://github.com/momentics/NanoVNA-X",
    "Version: " NANOVNA_VERSION_STRING " ["
    "p:" define_to_STR(
        SWEEP_POINTS_MAX) ", "
                          "IF:" define_to_STR(
                              FREQUENCY_IF_K) "k, "
                                              "ADC:" define_to_STR(
                                                  AUDIO_ADC_FREQ_K1) "k, "
                                                                     "Lcd:" define_to_STR(LCD_WIDTH) "x" define_to_STR(
                                                                         LCD_HEIGHT) "]",
    "Build Time: " __DATE__ " - " __TIME__,
    //  "Kernel: " CH_KERNEL_VERSION,
    //  "Compiler: " PORT_COMPILER_NAME,
    "Architecture: " PORT_ARCHITECTURE_NAME " Core Variant: " PORT_CORE_VARIANT_NAME,
    //  "Port Info: " PORT_INFO,
    "Platform: " PLATFORM_NAME,
    0 // sentinel
};

// Allow draw some debug on LCD
#ifdef DEBUG_CONSOLE_SHOW
void my_debug_log(int offs, char* log) {
  static uint16_t shell_line_y = 0;
  lcd_set_foreground(LCD_FG_COLOR);
  lcd_set_background(LCD_BG_COLOR);
  lcd_fill(FREQUENCIES_XPOS1, shell_line_y, LCD_WIDTH - FREQUENCIES_XPOS1, 2 * FONT_GET_HEIGHT);
  lcd_drawstring(FREQUENCIES_XPOS1 + offs, shell_line_y, log);
  shell_line_y += FONT_STR_HEIGHT;
  if (shell_line_y >= LCD_HEIGHT - FONT_STR_HEIGHT * 4)
    shell_line_y = 0;
}
#define DEBUG_LOG(offs, text) my_debug_log(offs, text);
#else
#define DEBUG_LOG(offs, text)
#endif

static void app_process_event_queue(systime_t timeout) {
  while (event_bus_dispatch(&app_event_bus, TIME_IMMEDIATE)) {
  }
  if (timeout != TIME_IMMEDIATE) {
    if (event_bus_dispatch(&app_event_bus, timeout)) {
      while (event_bus_dispatch(&app_event_bus, TIME_IMMEDIATE)) {
      }
    }
  }
}

static THD_WORKING_AREA(waThread1, 1024);
static THD_FUNCTION(Thread1, arg) {
  (void)arg;
  chRegSetThreadName("sweep");
#ifdef __FLIP_DISPLAY__
  if (VNA_MODE(VNA_MODE_FLIP_DISPLAY))
    lcd_set_flip(true);
#endif
  /*
   * UI (menu, touch, buttons) and plot initialize
   */
  ui_attach_event_bus(&app_event_bus);
  ui_init();
  // Initialize graph plotting
  plot_init();
  while (1) {
    app_process_event_queue(TIME_IMMEDIATE);
    shell_service_pending_commands();
    bool completed = false;
    uint16_t mask = measurement_pipeline_active_mask(&measurement_pipeline);
    if (sweep_mode & (SWEEP_ENABLE | SWEEP_ONCE)) {
      sweep_service_wait_for_copy_release();
      sweep_service_begin_measurement();
      event_bus_publish(&app_event_bus, EVENT_SWEEP_STARTED, &mask);
      completed = measurement_pipeline_execute(&measurement_pipeline, true, mask);
      sweep_mode &= ~SWEEP_ONCE;
      sweep_service_end_measurement();
    } else {
      sweep_service_end_measurement();
      app_process_event_queue(MS2ST(5));
    }
    app_process_event_queue(TIME_IMMEDIATE);
    // Process UI inputs
    sweep_mode |= SWEEP_UI_MODE;
    ui_process();
    sweep_mode &= ~SWEEP_UI_MODE;
    // Process collected data, calculate trace coordinates and plot only if scan completed
    if (completed) {
      sweep_service_increment_generation();
      event_bus_publish(&app_event_bus, EVENT_SWEEP_COMPLETED, &mask);
      //      START_PROFILE
      if ((props_mode & DOMAIN_MODE) == DOMAIN_TIME)
        app_measurement_transform_domain(mask);
      //      STOP_PROFILE;
    }
#ifndef DEBUG_CONSOLE_SHOW
    // plot trace and other indications as raster
    draw_all();
#endif
  }
}

void pause_sweep(void) {
  sweep_mode &= ~SWEEP_ENABLE;
}

static inline void resume_sweep(void) {
  sweep_mode |= SWEEP_ENABLE;
}

void toggle_sweep(void) {
  sweep_mode ^= SWEEP_ENABLE;
}

static void app_force_resume_sweep(void) {
  sweep_service_reset_progress();
  resume_sweep();
  sweep_mode |= SWEEP_ONCE;
}

config_t config = {
    .magic = CONFIG_MAGIC,
    ._harmonic_freq_threshold = FREQUENCY_THRESHOLD,
    ._IF_freq = FREQUENCY_OFFSET,
    ._touch_cal = DEFAULT_TOUCH_CONFIG,
    ._vna_mode = 0, // USB mode, search max
    ._brightness = DEFAULT_BRIGHTNESS,
    ._dac_value = 1922,
    ._vbat_offset = 420,
    ._bandwidth = BANDWIDTH_1000,
    ._lcd_palette = LCD_DEFAULT_PALETTE,
    ._serial_speed = SERIAL_DEFAULT_BITRATE,
    ._xtal_freq = XTALFREQ,
    ._measure_r = MEASURE_DEFAULT_R,
    ._lever_mode = LM_MARKER,
    ._band_mode = 0,
};

properties_t current_props;

// NanoVNA Default settings
static const trace_t def_trace[TRACES_MAX] = { // enable, type, channel, smith format, scale, refpos
    {TRUE, TRC_LOGMAG, 0, MS_RX, 10.0, NGRIDY - 1},
    {TRUE, TRC_LOGMAG, 1, MS_REIM, 10.0, NGRIDY - 1},
    {TRUE, TRC_SMITH, 0, MS_RX, 1.0, 0},
    {TRUE, TRC_PHASE, 1, MS_REIM, 90.0, NGRIDY / 2}};

static const marker_t def_markers[MARKERS_MAX] = {
    {TRUE, 0, 10 * SWEEP_POINTS_MAX / 100 - 1, 0},
#if MARKERS_MAX > 1
    {FALSE, 0, 20 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 2
    {FALSE, 0, 30 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 3
    {FALSE, 0, 40 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 4
    {FALSE, 0, 50 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 5
    {FALSE, 0, 60 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 6
    {FALSE, 0, 70 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
#if MARKERS_MAX > 7
    {FALSE, 0, 80 * SWEEP_POINTS_MAX / 100 - 1, 0},
#endif
};

// Load propeties default settings
static void load_default_properties(void) {
  // Magic add on caldata_save
  current_props.magic = PROPERTIES_MAGIC;
  current_props._frequency0 = 50000;     // start =  50kHz
  current_props._frequency1 = 900000000; // end   = 900MHz
  current_props._var_freq = 0;
  current_props._sweep_points = POINTS_COUNT_DEFAULT;     // Set default points count
  current_props._cal_frequency0 = 50000;                  // calibration start =  50kHz
  current_props._cal_frequency1 = 900000000;              // calibration end   = 900MHz
  current_props._cal_sweep_points = POINTS_COUNT_DEFAULT; // Set calibration default points count
  current_props._cal_status = 0;
  //=============================================
  memcpy(current_props._trace, def_trace, sizeof(def_trace));
  memcpy(current_props._markers, def_markers, sizeof(def_markers));
  //=============================================
  current_props._electrical_delay[0] = 0.0f;
  current_props._electrical_delay[1] = 0.0f;
  current_props._var_delay = 0.0f;
  current_props._s21_offset = 0.0f;
  current_props._portz = 50.0f;
  current_props._cal_load_r = 50.0f;
  current_props._velocity_factor = 70;
  current_props._current_trace = 0;
  current_props._active_marker = 0;
  current_props._previous_marker = MARKER_INVALID;
  current_props._mode = 0;
  current_props._reserved = 0;
  current_props._power = SI5351_CLK_DRIVE_STRENGTH_AUTO;
  current_props._cal_power = SI5351_CLK_DRIVE_STRENGTH_AUTO;
  current_props._measure = 0;
  // This data not loaded by default
  // current_props._cal_data[5][POINTS_COUNT][2];
  // Checksum add on caldata_save
  // current_props.checksum = 0;
}

//
// Backup registers support, allow save data on power off (while vbat power enabled)
//
#ifdef __USE_BACKUP__
#if SWEEP_POINTS_MAX > 511 || SAVEAREA_MAX > 15
#error "Check backup data limits!!"
#endif

// backup_0 bitfield
typedef union {
  struct {
    uint32_t points : 9;     //  9 !! limit 511 points!!
    uint32_t bw : 9;         // 18 !! limit 511
    uint32_t id : 4;         // 22 !! 15 save slots
    uint32_t leveler : 3;    // 25
    uint32_t brightness : 7; // 32
  };
  uint32_t v;
} backup_0;

void update_backup_data(void) {
  backup_0 bk = {.points = sweep_points,
                 .bw = config._bandwidth,
                 .id = lastsaveid,
                 .leveler = lever_mode,
                 .brightness = config._brightness};
  set_backup_data32(0, bk.v);
  set_backup_data32(1, frequency0);
  set_backup_data32(2, frequency1);
  set_backup_data32(3, var_freq);
  set_backup_data32(4, config._vna_mode);
}

static void load_settings(void) {
  load_default_properties(); // Load default settings
  if (config_recall() == 0 &&
      VNA_MODE(VNA_MODE_BACKUP)) { // Config loaded ok and need restore backup if enabled
    backup_0 bk = {.v = get_backup_data32(0)};
    if (bk.v != 0) {                                            // if backup data valid
      if (bk.id < SAVEAREA_MAX && caldata_recall(bk.id) == 0) { // Slot valid and Load ok
        sweep_points = bk.points; // Restore settings depend from calibration data
        frequency0 = get_backup_data32(1);
        frequency1 = get_backup_data32(2);
        var_freq = get_backup_data32(3);
      } else
        caldata_recall(0);
      // Here need restore settings not depend from cal data
      config._brightness = bk.brightness;
      lever_mode = bk.leveler;
      config._vna_mode = get_backup_data32(4) | (1 << VNA_MODE_BACKUP); // refresh backup settings
      set_bandwidth(bk.bw);
    } else
      caldata_recall(0); // Try load 0 slot
  } else
    caldata_recall(0); // Try load 0 slot
  app_measurement_update_frequencies();
#ifdef __VNA_MEASURE_MODULE__
  plot_set_measure_mode(current_props._measure);
#endif
}
#else
static void load_settings(void) {
  load_default_properties();
  config_recall();
  load_properties(0);
}
#endif

int load_properties(uint32_t id) {
  int r = caldata_recall(id);
  app_measurement_update_frequencies();
#ifdef __VNA_MEASURE_MODULE__
  plot_set_measure_mode(current_props._measure);
#endif
  return r;
}

VNA_SHELL_FUNCTION(cmd_pause) {
  (void)argc;
  (void)argv;
  pause_sweep();
}

VNA_SHELL_FUNCTION(cmd_resume) {
  (void)argc;
  (void)argv;

  // restore frequencies array and cal
  app_measurement_update_frequencies();
  resume_sweep();
}

VNA_SHELL_FUNCTION(cmd_reset) {
  (void)argc;
  (void)argv;
#ifdef __DFU_SOFTWARE_MODE__
  if (argc == 1) {
    if (get_str_index(argv[0], "dfu") == 0) {
      shell_printf("Performing reset to DFU mode" VNA_SHELL_NEWLINE_STR);
      ui_enter_dfu();
      return;
    }
  }
#endif
  shell_printf("Performing reset" VNA_SHELL_NEWLINE_STR);
  NVIC_SystemReset();
}

#ifdef __USE_SMOOTH__
VNA_SHELL_FUNCTION(cmd_smooth) {
  if (argc != 1) {
    shell_printf("usage: %s" VNA_SHELL_NEWLINE_STR "current: %u" VNA_SHELL_NEWLINE_STR,
                 "smooth {0-8}", get_smooth_factor());
    return;
  }
  set_smooth_factor(my_atoui(argv[0]));
}
#endif

#if ENABLE_CONFIG_COMMAND
VNA_SHELL_FUNCTION(cmd_config) {
  static const char cmd_mode_list[] = "auto"
#ifdef __USE_SMOOTH__
                                      "|avg"
#endif
#ifdef __USE_SERIAL_CONSOLE__
                                      "|connection"
#endif
                                      "|mode"
                                      "|grid"
                                      "|dot"
#ifdef __USE_BACKUP__
                                      "|bk"
#endif
#ifdef __FLIP_DISPLAY__
                                      "|flip"
#endif
#ifdef __DIGIT_SEPARATOR__
                                      "|separator"
#endif
      ;
  int idx;
  if (argc == 2 && (idx = get_str_index(argv[0], cmd_mode_list)) >= 0) {
    apply_vna_mode(idx, my_atoui(argv[1]));
  } else
    shell_printf("usage: config {%s} [0|1]" VNA_SHELL_NEWLINE_STR, cmd_mode_list);
}
#endif

#ifdef __VNA_MEASURE_MODULE__
VNA_SHELL_FUNCTION(cmd_measure) {
  static const char cmd_measure_list[] = "none"
#ifdef __USE_LC_MATCHING__
                                         "|lc" // Add LC match function
#endif
#ifdef __S21_MEASURE__
                                         "|lcshunt"  // Enable LC shunt measure option
                                         "|lcseries" // Enable LC series  measure option
                                         "|xtal"     // Enable XTAL measure option
                                         "|filter"   // Enable filter measure option
#endif
#ifdef __S11_CABLE_MEASURE__
                                         "|cable" // Enable S11 cable measure option
#endif
#ifdef __S11_RESONANCE_MEASURE__
                                         "|resonance" // Enable S11 resonance search option
#endif
      ;
  int idx;
  if (argc == 1 && (idx = get_str_index(argv[0], cmd_measure_list)) >= 0)
    plot_set_measure_mode(idx);
  else
    shell_printf("usage: measure {%s}" VNA_SHELL_NEWLINE_STR, cmd_measure_list);
}
#endif

#ifdef USE_VARIABLE_OFFSET
VNA_SHELL_FUNCTION(cmd_offset) {
  if (argc != 1) {
    shell_printf("usage: %s" VNA_SHELL_NEWLINE_STR "current: %u" VNA_SHELL_NEWLINE_STR,
                 "offset {frequency offset(Hz)}", IF_OFFSET);
    return;
  }
  si5351_set_frequency_offset(my_atoi(argv[0]));
}
#endif

VNA_SHELL_FUNCTION(cmd_freq) {
  if (argc != 1) {
    shell_printf("usage: freq {frequency(Hz)}" VNA_SHELL_NEWLINE_STR);
    return;
  }
  uint32_t freq = my_atoui(argv[0]);
  pause_sweep();
  app_measurement_set_frequency(freq);
  return;
}

void set_power(uint8_t value) {
  request_to_redraw(REDRAW_CAL_STATUS);
  if (value > SI5351_CLK_DRIVE_STRENGTH_8MA)
    value = SI5351_CLK_DRIVE_STRENGTH_AUTO;
  if (current_props._power == value)
    return;
  current_props._power = value;
  // Update power if pause, need for generation in CW mode
  if (!(sweep_mode & SWEEP_ENABLE))
    si5351_set_power(value);
}

VNA_SHELL_FUNCTION(cmd_power) {
  if (argc != 1) {
    shell_printf("usage: power {0-3}|{255 - auto}" VNA_SHELL_NEWLINE_STR
                 "power: %d" VNA_SHELL_NEWLINE_STR,
                 current_props._power);
    return;
  }
  set_power(my_atoi(argv[0]));
}

#ifdef __USE_RTC__
VNA_SHELL_FUNCTION(cmd_time) {
  (void)argc;
  (void)argv;
  uint32_t dt_buf[2];
  dt_buf[0] = rtc_get_tr_bcd(); // TR should be read first for sync
  dt_buf[1] = rtc_get_dr_bcd(); // DR should be read second
  static const uint8_t idx_to_time[] = {6, 5, 4, 2, 1, 0};
  static const char time_cmd[] = "y|m|d|h|min|sec|ppm";
  //            0    1   2       4      5     6
  // time[] ={sec, min, hr, 0, day, month, year, 0}
  uint8_t* time = (uint8_t*)dt_buf;
  if (argc == 3 && get_str_index(argv[0], "b") == 0) {
    rtc_set_time(my_atoui(argv[1]), my_atoui(argv[2]));
    return;
  }
  if (argc != 2)
    goto usage;
  int idx = get_str_index(argv[0], time_cmd);
  if (idx == 6) {
    rtc_set_cal(my_atof(argv[1]));
    return;
  }
  uint32_t val = my_atoui(argv[1]);
  if (idx < 0 || val > 99)
    goto usage;
  // Write byte value in struct
  time[idx_to_time[idx]] = ((val / 10) << 4) | (val % 10); // value in bcd format
  rtc_set_time(dt_buf[1], dt_buf[0]);
  return;
usage:
  shell_printf("20%02x/%02x/%02x %02x:%02x:%02x" VNA_SHELL_NEWLINE_STR
               "usage: time {[%s] 0-99} or {b 0xYYMMDD 0xHHMMSS}" VNA_SHELL_NEWLINE_STR,
               time[6], time[5], time[4], time[2], time[1], time[0], time_cmd);
}
#endif

#ifdef __VNA_ENABLE_DAC__
VNA_SHELL_FUNCTION(cmd_dac) {
  if (argc != 1) {
    shell_printf("usage: %s" VNA_SHELL_NEWLINE_STR "current: %u" VNA_SHELL_NEWLINE_STR,
                 "dac {value(0-4095)}", config._dac_value);
    return;
  }
  dac_setvalue_ch2(my_atoui(argv[0]) & 0xFFF);
}
#endif

VNA_SHELL_FUNCTION(cmd_threshold) {
  uint32_t value;
  if (argc != 1) {
    shell_printf("usage: %s" VNA_SHELL_NEWLINE_STR "current: %u" VNA_SHELL_NEWLINE_STR,
                 "threshold {frequency in harmonic mode}", config._harmonic_freq_threshold);
    return;
  }
  value = my_atoui(argv[0]);
  config._harmonic_freq_threshold = value;
  config_service_notify_configuration_changed();
}

VNA_SHELL_FUNCTION(cmd_saveconfig) {
  (void)argc;
  (void)argv;
  config_save();
  shell_printf("Config saved" VNA_SHELL_NEWLINE_STR);
}

VNA_SHELL_FUNCTION(cmd_clearconfig) {
  if (argc != 1) {
    shell_printf("usage: clearconfig {protection key}" VNA_SHELL_NEWLINE_STR);
    return;
  }

  if (get_str_index(argv[0], "1234") != 0) {
    shell_printf("Key unmatched." VNA_SHELL_NEWLINE_STR);
    return;
  }

  clear_all_config_prop_data();
  shell_printf(
      "Config and all cal data cleared." VNA_SHELL_NEWLINE_STR
      "Do reset manually to take effect. Then do touch cal and save." VNA_SHELL_NEWLINE_STR);
}

VNA_SHELL_FUNCTION(cmd_data) {
  int sel = 0;
  const float (*array)[2];
  osalSysLock();
  uint16_t points = sweep_points;
  osalSysUnlock();
  if (argc == 1) {
    sel = my_atoi(argv[0]);
  }
  if (sel < 0 || sel >= 7) {
    goto usage;
  }

  if (sel < 2) {
    sweep_service_snapshot_t snapshot;

    sweep_service_wait_for_generation();
    while (true) {
      if (!sweep_service_snapshot_acquire((uint8_t)sel, &snapshot)) {
        chThdSleepMilliseconds(1);
        continue;
      }

      for (uint16_t i = 0; i < snapshot.points; i++) {
        shell_printf("%f %f" VNA_SHELL_NEWLINE_STR, snapshot.data[i][0], snapshot.data[i][1]);
        if ((i & 0x0F) == 0x0F) {
          chThdYield();
        }
      }

      if (sweep_service_snapshot_release(&snapshot)) {
        points = snapshot.points;
        return;
      }
      chThdYield();
    }
  } else {
    array = cal_data[sel - 2];
    osalSysLock();
    points = cal_sweep_points;
    osalSysUnlock();
  }

  for (uint16_t i = 0; i < points; i++) {
    shell_printf("%f %f" VNA_SHELL_NEWLINE_STR, array[i][0], array[i][1]);
    if ((i & 0x0F) == 0x0F) {
      chThdYield();
    }
  }
  return;

usage:
  shell_printf("usage: data [array]" VNA_SHELL_NEWLINE_STR);
}

#ifdef __CAPTURE_RLE8__
void capture_rle8(void) {
  static const struct {
    uint16_t header;
    uint16_t width, height;
    uint8_t bit_per_pixel, compression;
  } screenshot_header = {0x4D42, LCD_WIDTH, LCD_HEIGHT, 8, 1};

  uint16_t size = sizeof(config._lcd_palette);
  shell_stream_write(&screenshot_header, sizeof(screenshot_header)); // write header
  shell_stream_write(&size, sizeof(uint16_t));                       // write palette block size
  shell_stream_write(config._lcd_palette, size);                     // write palette block
  uint16_t* data = &spi_buffer[32]; // most bad pack situation increase on 1 byte every 128, so put
                                    // not compressed data on 64 byte offset
  for (int y = 0, idx = 0; y < LCD_HEIGHT; y++) {
    lcd_read_memory(0, y, LCD_WIDTH, 1, data);   // read in 16bpp format
    for (int x = 0; x < LCD_WIDTH; x++) {        // convert to palette mode
      if (config._lcd_palette[idx] != data[x]) { // search color in palette
        for (idx = 0; idx < MAX_PALETTE && config._lcd_palette[idx] != data[x]; idx++)
          ;
        if (idx >= MAX_PALETTE)
          idx = 0;
      }
      ((uint8_t*)data)[x] = idx; // put palette index
    }
    spi_buffer[0] = packbits((char*)data, (char*)&spi_buffer[1], LCD_WIDTH); // pack
    shell_stream_write(spi_buffer, spi_buffer[0] + sizeof(uint16_t));
  }
}
#endif

VNA_SHELL_FUNCTION(cmd_capture) {
  (void)argc;
  (void)argv;
#ifdef __CAPTURE_RLE8__
  if (argc > 0) {
    capture_rle8();
    return;
  }
#endif
// Check buffer limits, if less possible reduce rows count
#define READ_ROWS 2
#if (SPI_BUFFER_SIZE * LCD_PIXEL_SIZE) < (LCD_RX_PIXEL_SIZE * LCD_WIDTH * READ_ROWS)
#error "Low size of spi_buffer for cmd_capture"
#endif
  // read 2 row pixel time
  for (int y = 0; y < LCD_HEIGHT; y += READ_ROWS) {
    // use uint16_t spi_buffer[2048] (defined in ili9341) for read buffer
    lcd_read_memory(0, y, LCD_WIDTH, READ_ROWS, (uint16_t*)spi_buffer);
    shell_stream_write(spi_buffer, READ_ROWS * LCD_WIDTH * sizeof(uint16_t));
  }
}

static void (*sample_func)(float* gamma) = calculate_gamma;
#if ENABLE_SAMPLE_COMMAND
VNA_SHELL_FUNCTION(cmd_sample) {
  if (argc != 1)
    goto usage;
  //                                         0    1   2
  static const char cmd_sample_list[] = "gamma|ampl|ref";
  switch (get_str_index(argv[0], cmd_sample_list)) {
  case 0:
    sample_func = calculate_gamma;
    return;
  case 1:
    sample_func = fetch_amplitude;
    return;
  case 2:
    sample_func = fetch_amplitude_ref;
    return;
  default:
    break;
  }
usage:
  shell_printf("usage: sample {%s}" VNA_SHELL_NEWLINE_STR, cmd_sample_list);
}
#endif

void set_bandwidth(uint16_t bw_count) {
  config._bandwidth = bw_count & 0x1FF;
  request_to_redraw(REDRAW_BACKUP | REDRAW_FREQUENCY);
  config_service_notify_configuration_changed();
}

uint32_t get_bandwidth_frequency(uint16_t bw_freq) {
  return (AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT) / (bw_freq + 1);
}

#define MAX_BANDWIDTH (AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT)
#define MIN_BANDWIDTH ((AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT) / 512 + 1)

VNA_SHELL_FUNCTION(cmd_bandwidth) {
  uint16_t user_bw;
  if (argc == 1)
    user_bw = my_atoui(argv[0]);
  else if (argc == 2) {
    uint16_t f = my_atoui(argv[0]);
    if (f > MAX_BANDWIDTH)
      user_bw = 0;
    else if (f < MIN_BANDWIDTH)
      user_bw = 511;
    else
      user_bw = ((AUDIO_ADC_FREQ + AUDIO_SAMPLES_COUNT / 2) / AUDIO_SAMPLES_COUNT) / f - 1;
  } else
    goto result;
  set_bandwidth(user_bw);
result:
  shell_printf("bandwidth %d (%uHz)" VNA_SHELL_NEWLINE_STR, config._bandwidth,
               get_bandwidth_frequency(config._bandwidth));
}

#if ENABLE_GAIN_COMMAND
VNA_SHELL_FUNCTION(cmd_gain) {
  if (argc == 0 || argc > 2) {
    shell_printf("usage: gain {lgain(0-95)} [rgain(0-95)]" VNA_SHELL_NEWLINE_STR);
    return;
  }
  int lvalue = my_atoui(argv[0]);
  int rvalue = (argc == 2) ? my_atoui(argv[1]) : lvalue;
  tlv320aic3204_set_gain(lvalue, rvalue);
}
#endif

void set_sweep_points(uint16_t points) {
  if (points > SWEEP_POINTS_MAX)
    points = SWEEP_POINTS_MAX;
  if (points < SWEEP_POINTS_MIN)
    points = SWEEP_POINTS_MIN;
  if (points == sweep_points)
    return;
  sweep_points = points;
  app_measurement_update_frequencies();
}

/*
 * Frequency list functions
 */
static bool need_interpolate(freq_t start, freq_t stop, uint16_t points) {
  return start != cal_frequency0 || stop != cal_frequency1 || points != cal_sweep_points;
}

#define SCAN_MASK_OUT_FREQ 0b00000001
#define SCAN_MASK_OUT_DATA0 0b00000010
#define SCAN_MASK_OUT_DATA1 0b00000100
#define SCAN_MASK_NO_CALIBRATION 0b00001000
#define SCAN_MASK_NO_EDELAY 0b00010000
#define SCAN_MASK_NO_S21OFFS 0b00100000
#define SCAN_MASK_BINARY 0b10000000

VNA_SHELL_FUNCTION(cmd_scan) {
  freq_t start, stop;
  uint16_t points = sweep_points;
  const freq_t original_start = get_sweep_frequency(ST_START);
  const freq_t original_stop = get_sweep_frequency(ST_STOP);
  const uint16_t original_points = sweep_points;
  const uint32_t original_props_mode = props_mode;
  const freq_t saved_frequency0 = frequency0;
  const freq_t saved_frequency1 = frequency1;
  bool restore_config = false;

  if (argc < 2 || argc > 4) {
    shell_printf("usage: scan {start(Hz)} {stop(Hz)} [points] [outmask]" VNA_SHELL_NEWLINE_STR);
    return;
  }

  start = my_atoui(argv[0]);
  stop = my_atoui(argv[1]);
  if (start == 0 || stop == 0 || start > stop) {
    shell_printf("frequency range is invalid" VNA_SHELL_NEWLINE_STR);
    return;
  }
  if (start != original_start || stop != original_stop)
    restore_config = true;
  if (argc >= 3) {
    points = my_atoui(argv[2]);
    if (points == 0 || points > SWEEP_POINTS_MAX) {
      shell_printf("sweep points exceeds range " define_to_STR(SWEEP_POINTS_MAX)
                       VNA_SHELL_NEWLINE_STR);
      return;
    }
    if (points != original_points)
      restore_config = true;
  }
  uint16_t mask = 0;
  uint16_t sweep_ch = SWEEP_CH0_MEASURE | SWEEP_CH1_MEASURE;

  FREQ_STARTSTOP();
  if (props_mode != original_props_mode) {
    restore_config = true;
  }
  frequency0 = start;
  frequency1 = stop;
  sweep_points = points;
  app_measurement_update_frequencies();

#if ENABLE_SCANBIN_COMMAND
  if (argc == 4) {
    mask = my_atoui(argv[3]);
    if (sweep_mode & SWEEP_BINARY)
      mask |= SCAN_MASK_BINARY;
    sweep_ch = (mask >> 1) & 3;
  }
  sweep_mode &= ~(SWEEP_BINARY);
#else
  if (argc == 4) {
    mask = my_atoui(argv[3]);
    sweep_ch = (mask >> 1) & 3;
  }
#endif

  if ((cal_status & CALSTAT_APPLY) && !(mask & SCAN_MASK_NO_CALIBRATION))
    sweep_ch |= SWEEP_APPLY_CALIBRATION;
  if (electrical_delayS11 && !(mask & SCAN_MASK_NO_EDELAY))
    sweep_ch |= SWEEP_APPLY_EDELAY_S11;
  if (electrical_delayS21 && !(mask & SCAN_MASK_NO_EDELAY))
    sweep_ch |= SWEEP_APPLY_EDELAY_S21;
  if (s21_offset && !(mask & SCAN_MASK_NO_S21OFFS))
    sweep_ch |= SWEEP_APPLY_S21_OFFSET;

  if (need_interpolate(start, stop, sweep_points))
    sweep_ch |= SWEEP_USE_INTERPOLATION;

  if (sweep_ch & (SWEEP_CH0_MEASURE | SWEEP_CH1_MEASURE))
    app_measurement_sweep(false, sweep_ch);
  pause_sweep();
  // Output data after if set (faster data receive)
  if (mask) {
    if (mask & SCAN_MASK_BINARY) {
      shell_stream_write(&mask, sizeof(uint16_t));
      shell_stream_write(&points, sizeof(uint16_t));
      for (int i = 0; i < points; i++) {
        if (mask & SCAN_MASK_OUT_FREQ) {
          freq_t f = get_frequency(i);
          shell_stream_write(&f, sizeof(freq_t));
        } // 4 bytes .. frequency
        if (mask & SCAN_MASK_OUT_DATA0)
          shell_stream_write(&measured[0][i][0], sizeof(float) * 2); // 4+4 bytes .. S11 real/imag
        if (mask & SCAN_MASK_OUT_DATA1)
          shell_stream_write(&measured[1][i][0], sizeof(float) * 2); // 4+4 bytes .. S21 real/imag
      }
    } else {
      for (int i = 0; i < points; i++) {
        if (mask & SCAN_MASK_OUT_FREQ)
          shell_printf(VNA_FREQ_FMT_STR " ", get_frequency(i));
        if (mask & SCAN_MASK_OUT_DATA0)
          shell_printf("%f %f ", measured[0][i][0], measured[0][i][1]);
        if (mask & SCAN_MASK_OUT_DATA1)
          shell_printf("%f %f ", measured[1][i][0], measured[1][i][1]);
        shell_printf(VNA_SHELL_NEWLINE_STR);
      }
    }
  }

  if (restore_config) {
    props_mode = original_props_mode;
    frequency0 = saved_frequency0;
    frequency1 = saved_frequency1;
    sweep_points = original_points;
    app_measurement_update_frequencies();
  }
}

#if ENABLE_SCANBIN_COMMAND
VNA_SHELL_FUNCTION(cmd_scan_bin) {
  sweep_mode |= SWEEP_BINARY;
  cmd_scan(argc, argv);
  sweep_mode &= ~(SWEEP_BINARY);
}
#endif

VNA_SHELL_FUNCTION(cmd_tcxo) {
  if (argc != 1) {
    shell_printf("usage: %s" VNA_SHELL_NEWLINE_STR "current: %u" VNA_SHELL_NEWLINE_STR,
                 "tcxo {TCXO frequency(Hz)}", config._xtal_freq);
    return;
  }
  si5351_set_tcxo(my_atoui(argv[0]));
}

void set_marker_index(int m, int idx) {
  if (m == MARKER_INVALID || (uint32_t)idx >= sweep_points)
    return;
  markers[m].frequency = get_frequency(idx);
  if (markers[m].index == idx)
    return;
  request_to_draw_marker(markers[m].index); // Mark old marker position for erase
  markers[m].index = idx;                   // Set new position
  request_to_redraw(REDRAW_MARKER);
}

freq_t get_marker_frequency(int marker) {
  if ((uint32_t)marker >= MARKERS_MAX)
    return 0;
  return markers[marker].frequency;
}

static void update_marker_index(freq_t fstart, freq_t fstop, uint16_t points) {
  int m, idx;
  for (m = 0; m < MARKERS_MAX; m++) {
    // Update index for all markers !!
    freq_t f = markers[m].frequency;
    if (f == 0)
      idx = markers[m].index; // Not need update index in no freq
    else if (f <= fstart)
      idx = 0;
    else if (f >= fstop)
      idx = points - 1;
    else { // Search frequency index for marker frequency
      float r = ((float)(f - fstart)) / (fstop - fstart);
      idx = r * (points - 1);
    }
    set_marker_index(m, idx);
  }
}

static inline void sweep_get_ordered(freq_t* start, freq_t* stop) {
  freq_t a = frequency0;
  freq_t b = frequency1;
  if (a <= b) {
    *start = a;
    *stop = b;
  } else {
    *start = b;
    *stop = a;
  }
}

void app_measurement_update_frequencies(void) {
  freq_t start, stop;

  sweep_get_ordered(&start, &stop);

  app_measurement_set_frequencies(start, stop, sweep_points);

  update_marker_index(start, stop, sweep_points);
  // set grid layout
  update_grid(start, stop);
  // Update interpolation flag
  if (need_interpolate(start, stop, sweep_points))
    cal_status |= CALSTAT_INTERPOLATED;
  else
    cal_status &= ~CALSTAT_INTERPOLATED;

  request_to_redraw(REDRAW_BACKUP | REDRAW_PLOT | REDRAW_CAL_STATUS | REDRAW_FREQUENCY |
                    REDRAW_AREA);
  sweep_service_reset_progress();
  if ((sweep_mode & SWEEP_ENABLE) == 0U) {
    sweep_mode |= SWEEP_ONCE;
  }
  event_bus_publish(&app_event_bus, EVENT_SWEEP_CONFIGURATION_CHANGED, NULL);
}

static void set_sweep_frequency_internal(uint16_t type, freq_t freq, bool enforce_order) {
  // Check frequency for out of bounds (minimum SPAN can be any value)
  if (type < ST_SPAN && freq < FREQUENCY_MIN)
    freq = FREQUENCY_MIN;
  // One point step input, so change stop freq or span depend from mode
  if (type == ST_STEP) {
    freq *= (sweep_points - 1);
    type = FREQ_IS_CENTERSPAN() ? ST_SPAN : ST_STOP;
    if (type == ST_STOP)
      freq += frequency0;
  }
  if (type != ST_VAR && freq > FREQUENCY_MAX)
    freq = FREQUENCY_MAX;
  freq_t center, span, start, stop;
  switch (type) {
  case ST_START:
    FREQ_STARTSTOP();
    frequency0 = freq;
    if (enforce_order && frequency1 < freq)
      frequency1 = freq;
    break;
  case ST_STOP:
    FREQ_STARTSTOP()
    frequency1 = freq;
    if (enforce_order && frequency0 > freq)
      frequency0 = freq;
    break;
  case ST_CENTER:
    FREQ_CENTERSPAN();
    sweep_get_ordered(&start, &stop);
    center = freq;
    span = (stop - start + 1) >> 1;
    if (span > center - FREQUENCY_MIN)
      span = center - FREQUENCY_MIN;
    if (span > FREQUENCY_MAX - center)
      span = FREQUENCY_MAX - center;
    frequency0 = center - span;
    frequency1 = center + span;
    break;
  case ST_SPAN:
    FREQ_CENTERSPAN();
    sweep_get_ordered(&start, &stop);
    center = (freq_t)(((uint64_t)start + (uint64_t)stop) >> 1);
    span = freq >> 1;
    if (center < FREQUENCY_MIN + span)
      center = FREQUENCY_MIN + span;
    if (center > FREQUENCY_MAX - span)
      center = FREQUENCY_MAX - span;
    frequency0 = center - span;
    frequency1 = center + span;
    break;
  case ST_CW:
    FREQ_CENTERSPAN();
    frequency0 = freq;
    frequency1 = freq;
    break;
  case ST_VAR:
    var_freq = freq;
    request_to_redraw(REDRAW_BACKUP);
    return;
  }
  app_measurement_update_frequencies();
}

void set_sweep_frequency(uint16_t type, freq_t freq) {
  set_sweep_frequency_internal(type, freq, true);
}

void reset_sweep_frequency(void) {
  frequency0 = cal_frequency0;
  frequency1 = cal_frequency1;
  sweep_points = cal_sweep_points;
  app_measurement_update_frequencies();
}

VNA_SHELL_FUNCTION(cmd_sweep) {
  if (argc == 0) {
    shell_printf(VNA_FREQ_FMT_STR " " VNA_FREQ_FMT_STR " %d" VNA_SHELL_NEWLINE_STR,
                 get_sweep_frequency(ST_START), get_sweep_frequency(ST_STOP), sweep_points);
    return;
  } else if (argc > 3) {
    goto usage;
  }
  freq_t value0 = 0;
  freq_t value1 = 0;
  uint32_t value2 = 0;
  if (argc >= 1)
    value0 = my_atoui(argv[0]);
  if (argc >= 2)
    value1 = my_atoui(argv[1]);
  if (argc >= 3)
    value2 = my_atoui(argv[2]);
#if MAX_FREQ_TYPE != 5
#error "Sweep mode possibly changed, check cmd_sweep function"
#endif
  // Parse sweep {start|stop|center|span|cw|step|var} {freq(Hz)}
  // get enum ST_START, ST_STOP, ST_CENTER, ST_SPAN, ST_CW, ST_STEP, ST_VAR
  static const char sweep_cmd[] = "start|stop|center|span|cw|step|var";
  if (argc == 2 && value0 == 0) {
    int type = get_str_index(argv[0], sweep_cmd);
    if (type == -1)
      goto usage;
    bool enforce = !(type == ST_START || type == ST_STOP);
    set_sweep_frequency_internal(type, value1, enforce);
    return;
  }
  //  Parse sweep {start(Hz)} [stop(Hz)]
  if (value0)
    set_sweep_frequency_internal(ST_START, value0, false);
  if (value1)
    set_sweep_frequency_internal(ST_STOP, value1, false);
  if (value2)
    set_sweep_points(value2);
  return;
usage:
  shell_printf("usage: sweep {start(Hz)} [stop(Hz)] [points]" VNA_SHELL_NEWLINE_STR
               "\tsweep {%s} {freq(Hz)}" VNA_SHELL_NEWLINE_STR,
               sweep_cmd);
}

static void eterm_set(int term, float re, float im) {
  int i;
  for (i = 0; i < sweep_points; i++) {
    cal_data[term][i][0] = re;
    cal_data[term][i][1] = im;
  }
}

static void eterm_copy(int dst, int src) {
  memcpy(cal_data[dst], cal_data[src], sizeof cal_data[dst]);
}

static void eterm_calc_es(void) {
  int i;
  for (i = 0; i < sweep_points; i++) {
    // z=1/(jwc*z0) = 1/(2*pi*f*c*z0)  Note: normalized with Z0
    // s11ao = (z-1)/(z+1) = (1-1/z)/(1+1/z) = (1-jwcz0)/(1+jwcz0)
    // prepare 1/s11ao for effeiciency
    float s11aor = 1.0f;
    float s11aoi = 0.0f;
    // S11mo’= S11mo - Ed
    // S11ms’= S11ms - Ed
    float s11or = cal_data[CAL_OPEN][i][0] - cal_data[ETERM_ED][i][0];
    float s11oi = cal_data[CAL_OPEN][i][1] - cal_data[ETERM_ED][i][1];
    float s11sr = cal_data[CAL_SHORT][i][0] - cal_data[ETERM_ED][i][0];
    float s11si = cal_data[CAL_SHORT][i][1] - cal_data[ETERM_ED][i][1];
    // Es = (S11mo'/s11ao + S11ms’)/(S11mo' - S11ms’)
    float numr = s11sr + s11or * s11aor - s11oi * s11aoi;
    float numi = s11si + s11oi * s11aor + s11or * s11aoi;
    float denomr = s11or - s11sr;
    float denomi = s11oi - s11si;
    float d = denomr * denomr + denomi * denomi;
    cal_data[ETERM_ES][i][0] = (numr * denomr + numi * denomi) / d;
    cal_data[ETERM_ES][i][1] = (numi * denomr - numr * denomi) / d;
  }
  cal_status &= ~CALSTAT_OPEN;
  cal_status |= CALSTAT_ES;
}

static void eterm_calc_er(int sign) {
  int i;
  for (i = 0; i < sweep_points; i++) {
    // Er = sign*(1-sign*Es)S11ms'
    float s11sr = cal_data[CAL_SHORT][i][0] - cal_data[ETERM_ED][i][0];
    float s11si = cal_data[CAL_SHORT][i][1] - cal_data[ETERM_ED][i][1];
    float esr = cal_data[ETERM_ES][i][0];
    float esi = cal_data[ETERM_ES][i][1];
    if (sign > 0) {
      esr = -esr;
      esi = -esi;
    }
    esr = 1 + esr;
    float err = esr * s11sr - esi * s11si;
    float eri = esr * s11si + esi * s11sr;
    if (sign < 0) {
      err = -err;
      eri = -eri;
    }
    cal_data[ETERM_ER][i][0] = err;
    cal_data[ETERM_ER][i][1] = eri;
  }
  cal_status &= ~CALSTAT_SHORT;
  cal_status |= CALSTAT_ER;
}

// CAUTION: Et is inversed for efficiency
static void eterm_calc_et(void) {
  int i;
  for (i = 0; i < sweep_points; i++) {
    // Et = 1/(S21mt - Ex)
    float etr = cal_data[CAL_THRU][i][0] - cal_data[CAL_ISOLN][i][0];
    float eti = cal_data[CAL_THRU][i][1] - cal_data[CAL_ISOLN][i][1];
    float sq = etr * etr + eti * eti;
    float invr = etr / sq;
    float invi = -eti / sq;
    cal_data[ETERM_ET][i][0] = invr;
    cal_data[ETERM_ET][i][1] = invi;
  }
  cal_status &= ~CALSTAT_THRU;
  cal_status |= CALSTAT_ET;
}

static void apply_ch0_error_term(float data[4], float c_data[CAL_TYPE_COUNT][2]) {
  // S11m' = S11m - Ed
  // S11a = S11m' / (Er + Es S11m')
  float s11mr = data[0] - c_data[ETERM_ED][0];
  float s11mi = data[1] - c_data[ETERM_ED][1];
  float err = c_data[ETERM_ER][0] + s11mr * c_data[ETERM_ES][0] - s11mi * c_data[ETERM_ES][1];
  float eri = c_data[ETERM_ER][1] + s11mr * c_data[ETERM_ES][1] + s11mi * c_data[ETERM_ES][0];
  float sq = err * err + eri * eri;
  data[0] = (s11mr * err + s11mi * eri) / sq;
  data[1] = (s11mi * err - s11mr * eri) / sq;
}

static void apply_ch1_error_term(float data[4], float c_data[CAL_TYPE_COUNT][2]) {
  // CAUTION: Et is inversed for efficiency
  // S21a = (S21m - Ex) * Et`
  float s21mr = data[2] - c_data[ETERM_EX][0];
  float s21mi = data[3] - c_data[ETERM_EX][1];
  // Not made CH1 correction by CH0 data
  data[2] = s21mr * c_data[ETERM_ET][0] - s21mi * c_data[ETERM_ET][1];
  data[3] = s21mi * c_data[ETERM_ET][0] + s21mr * c_data[ETERM_ET][1];
  if (cal_status & CALSTAT_ENHANCED_RESPONSE) {
    // S21a*= 1 - Es * S11a
    float esr = 1.0f - (c_data[ETERM_ES][0] * data[0] - c_data[ETERM_ES][1] * data[1]);
    float esi = 0.0f - (c_data[ETERM_ES][1] * data[0] + c_data[ETERM_ES][0] * data[1]);
    float re = data[2];
    float im = data[3];
    data[2] = esr * re - esi * im;
    data[3] = esi * re + esr * im;
  }
}

void cal_collect(uint16_t type) {
  uint16_t dst, src;

  static const struct {
    uint16_t set_flag;
    uint16_t clr_flag;
    uint8_t dst;
    uint8_t src;
  } calibration_set[] = {
      //    type       set data flag                              reset flag  destination source
      [CAL_LOAD] = {CALSTAT_LOAD, ~(CALSTAT_APPLY), CAL_LOAD, 0},
      [CAL_OPEN] = {CALSTAT_OPEN, ~(CALSTAT_ES | CALSTAT_ER | CALSTAT_APPLY), CAL_OPEN,
                    0}, // Reset Es and Er state
      [CAL_SHORT] = {CALSTAT_SHORT, ~(CALSTAT_ES | CALSTAT_ER | CALSTAT_APPLY), CAL_SHORT,
                     0}, // Reset Es and Er state
      [CAL_THRU] = {CALSTAT_THRU, ~(CALSTAT_ET | CALSTAT_APPLY), CAL_THRU, 1}, // Reset Et state
      [CAL_ISOLN] = {CALSTAT_ISOLN, ~(CALSTAT_APPLY), CAL_ISOLN, 1},
  };
  if (type >= ARRAY_COUNT(calibration_set))
    return;

  // reset old calibration if frequency range/points not some
  freq_t cal_start, cal_stop;
  sweep_get_ordered(&cal_start, &cal_stop);
  if (need_interpolate(cal_start, cal_stop, sweep_points)) {
    cal_status = 0;
    cal_frequency0 = cal_start;
    cal_frequency1 = cal_stop;
    cal_sweep_points = sweep_points;
  }
  cal_power = current_props._power;

  cal_status &= calibration_set[type].clr_flag;
  cal_status |= calibration_set[type].set_flag;
  dst = calibration_set[type].dst;
  src = calibration_set[type].src;

  // Run sweep for collect data (use minimum BANDWIDTH_30, or bigger if set)
  uint8_t bw = config._bandwidth; // store current setting
  if (bw < BANDWIDTH_100)
    config._bandwidth = BANDWIDTH_100;

  // Set MAX settings for sweep_points on calibrate
  //  if (sweep_points != POINTS_COUNT)
  //    set_sweep_points(POINTS_COUNT);
  uint16_t mask = (src == 0) ? SWEEP_CH0_MEASURE : SWEEP_CH1_MEASURE;
  //  if (electrical_delayS11) mask|= SWEEP_APPLY_EDELAY_S11;
  //  if (electrical_delayS21) mask|= SWEEP_APPLY_EDELAY_S21;
  // Measure calibration data
  app_measurement_sweep(false, mask);
  // Copy calibration data
  memcpy(cal_data[dst], measured[src], sizeof measured[0]);

  // Made average if need
  int count = 1, i, j;
  for (i = 1; i < count; i++) {
    app_measurement_sweep(false, (src == 0) ? SWEEP_CH0_MEASURE : SWEEP_CH1_MEASURE);
    for (j = 0; j < sweep_points; j++) {
      cal_data[dst][j][0] += measured[src][j][0];
      cal_data[dst][j][1] += measured[src][j][1];
    }
  }
  if (i != 1) {
    float k = 1.0f / i;
    for (j = 0; j < sweep_points; j++) {
      cal_data[dst][j][0] *= k;
      cal_data[dst][j][1] *= k;
    }
  }

  config._bandwidth = bw; // restore
  request_to_redraw(REDRAW_CAL_STATUS);
}

void cal_done(void) {
  // Set Load/Ed to default if not calculated
  if (!(cal_status & CALSTAT_LOAD))
    eterm_set(ETERM_ED, 0.0, 0.0);
  // Set Isoln/Ex to default if not measured
  if (!(cal_status & CALSTAT_ISOLN))
    eterm_set(ETERM_EX, 0.0, 0.0);

  // Precalculate Es and Er from Short and Open (and use Load/Ed data)
  if ((cal_status & CALSTAT_SHORT) && (cal_status & CALSTAT_OPEN)) {
    eterm_calc_es();
    eterm_calc_er(-1);
  } else if (cal_status & CALSTAT_OPEN) {
    eterm_copy(CAL_SHORT, CAL_OPEN);
    cal_status &= ~CALSTAT_OPEN;
    eterm_set(ETERM_ES, 0.0, 0.0);
    eterm_calc_er(1);
  } else if (cal_status & CALSTAT_SHORT) {
    eterm_set(ETERM_ES, 0.0, 0.0);
    eterm_calc_er(-1);
  }

  // Apply Et
  if (cal_status & CALSTAT_THRU)
    eterm_calc_et();

  // Set other fields to default if not set
  if (!(cal_status & CALSTAT_ET))
    eterm_set(ETERM_ET, 1.0, 0.0);
  if (!(cal_status & CALSTAT_ER))
    eterm_set(ETERM_ER, 1.0, 0.0);
  if (!(cal_status & CALSTAT_ES))
    eterm_set(ETERM_ES, 0.0, 0.0);

  cal_status |= CALSTAT_APPLY;
  lastsaveid = NO_SAVE_SLOT;
  request_to_redraw(REDRAW_BACKUP | REDRAW_CAL_STATUS);
}

static void cal_interpolate(int idx, freq_t f, float data[CAL_TYPE_COUNT][2]) {
  int eterm;
  uint16_t src_points = cal_sweep_points - 1;
  if (idx >= 0)
    goto copy_point;
  if (f <= cal_frequency0) {
    idx = 0;
    goto copy_point;
  }
  if (f >= cal_frequency1) {
    idx = src_points;
    goto copy_point;
  }
  // Calculate k for linear interpolation
  freq_t span = cal_frequency1 - cal_frequency0;
  idx = (uint64_t)(f - cal_frequency0) * (uint64_t)src_points / span;
  uint64_t v = (uint64_t)span * idx + src_points / 2;
  freq_t src_f0 = cal_frequency0 + (v) / src_points;
  freq_t src_f1 = cal_frequency0 + (v + span) / src_points;

  freq_t delta = src_f1 - src_f0;
  // Not need interpolate
  if (f == src_f0)
    goto copy_point;

  float k = (delta == 0) ? 0.0f : (float)(f - src_f0) / delta;
  // avoid glitch between freqs in different harmonics mode
  uint32_t hf0 = si5351_get_harmonic_lvl(src_f0);
  if (hf0 != si5351_get_harmonic_lvl(src_f1)) {
    // f in prev harmonic, need extrapolate from prev 2 points
    if (hf0 == si5351_get_harmonic_lvl(f)) {
      if (idx < 1)
        goto copy_point; // point limit
      idx--;
      k += 1.0f;
    }
    // f in next harmonic, need extrapolate from next 2 points
    else {
      if (idx >= src_points)
        goto copy_point; // point limit
      idx++;
      k -= 1.0f;
    }
  }
  // Interpolate by k
  for (eterm = 0; eterm < CAL_TYPE_COUNT; eterm++) {
    data[eterm][0] =
        cal_data[eterm][idx][0] + k * (cal_data[eterm][idx + 1][0] - cal_data[eterm][idx][0]);
    data[eterm][1] =
        cal_data[eterm][idx][1] + k * (cal_data[eterm][idx + 1][1] - cal_data[eterm][idx][1]);
  }
  return;
  // Direct point copy
copy_point:
  for (eterm = 0; eterm < CAL_TYPE_COUNT; eterm++) {
    data[eterm][0] = cal_data[eterm][idx][0];
    data[eterm][1] = cal_data[eterm][idx][1];
  }
  return;
}

VNA_SHELL_FUNCTION(cmd_cal) {
  static const char* items[] = {"load", "open", "short", "thru",  "isoln",
                                "Es",   "Er",   "Et",    "cal'ed"};

  if (argc == 0) {
    int i;
    for (i = 0; i < 9; i++) {
      if (cal_status & (1 << i))
        shell_printf("%s ", items[i]);
    }
    shell_printf(VNA_SHELL_NEWLINE_STR);
    return;
  }
  request_to_redraw(REDRAW_CAL_STATUS);
  //                                     0    1     2    3     4    5  6   7     8
  static const char cmd_cal_list[] = "load|open|short|thru|isoln|done|on|off|reset";
  switch (get_str_index(argv[0], cmd_cal_list)) {
  case 0:
    cal_collect(CAL_LOAD);
    return;
  case 1:
    cal_collect(CAL_OPEN);
    return;
  case 2:
    cal_collect(CAL_SHORT);
    return;
  case 3:
    cal_collect(CAL_THRU);
    return;
  case 4:
    cal_collect(CAL_ISOLN);
    return;
  case 5:
    cal_done();
    return;
  case 6:
    cal_status |= CALSTAT_APPLY;
    return;
  case 7:
    cal_status &= ~CALSTAT_APPLY;
    return;
  case 8:
    cal_status = 0;
    return;
  default:
    break;
  }
  shell_printf("usage: cal [%s]" VNA_SHELL_NEWLINE_STR, cmd_cal_list);
}

VNA_SHELL_FUNCTION(cmd_save) {
  uint32_t id;
  if (argc == 1 && (id = my_atoui(argv[0])) < SAVEAREA_MAX) {
    caldata_save(id);
    request_to_redraw(REDRAW_CAL_STATUS);
    return;
  }
  shell_printf("usage: %s 0..%d" VNA_SHELL_NEWLINE_STR, SAVEAREA_MAX - 1, "save");
}

VNA_SHELL_FUNCTION(cmd_recall) {
  uint32_t id;
  if (argc == 1 && (id = my_atoui(argv[0])) < SAVEAREA_MAX) {
    if (load_properties(id)) // Check for success
      shell_printf("Err, default load" VNA_SHELL_NEWLINE_STR);
    return;
  }
  shell_printf("usage: %s 0..%d" VNA_SHELL_NEWLINE_STR, SAVEAREA_MAX - 1, "recall");
}

static const char* const trc_channel_name[] = {"S11", "S21"};

const char* get_trace_chname(int t) {
  return trc_channel_name[trace[t].channel & 1];
}

void set_trace_type(int t, int type, int channel) {
  channel &= 1;
  bool update = trace[t].type != type || trace[t].channel != channel;
  if (!update)
    return;
  if (trace[t].type != type) {
    trace[t].type = type;
    // Set default trace refpos
    set_trace_refpos(t, trace_info_list[type].refpos);
    // Set default trace scale
    set_trace_scale(t, trace_info_list[type].scale_unit);
    request_to_redraw(REDRAW_AREA | REDRAW_PLOT | REDRAW_BACKUP); // need for update grid
  }
  set_trace_channel(t, channel);
}

void set_trace_channel(int t, int channel) {
  channel &= 1;
  if (trace[t].channel != channel) {
    trace[t].channel = channel;
    request_to_redraw(REDRAW_MARKER | REDRAW_PLOT);
  }
}

void set_active_trace(int t) {
  if (current_trace == t)
    return;
  current_trace = t;
  request_to_redraw(REDRAW_MARKER | REDRAW_GRID_VALUE);
}

void set_trace_scale(int t, float scale) {
  if (trace[t].scale != scale) {
    trace[t].scale = scale;
    request_to_redraw(REDRAW_MARKER | REDRAW_GRID_VALUE | REDRAW_PLOT);
  }
}

void set_trace_refpos(int t, float refpos) {
  if (trace[t].refpos != refpos) {
    trace[t].refpos = refpos;
    request_to_redraw(REDRAW_REFERENCE | REDRAW_GRID_VALUE | REDRAW_PLOT);
  }
}

void set_trace_enable(int t, bool enable) {
  trace[t].enabled = enable;
  current_trace = enable ? t : TRACE_INVALID;
  if (!enable) {
    for (int i = 0; i < TRACES_MAX; i++) // set first enabled as current trace
      if (trace[i].enabled) {
        set_active_trace(i);
        break;
      }
  }
  request_to_redraw(REDRAW_AREA);
}

void set_electrical_delay(int ch, float seconds) {
  if (current_props._electrical_delay[ch] == seconds)
    return;
  current_props._electrical_delay[ch] = seconds;
  request_to_redraw(REDRAW_MARKER);
}

float get_electrical_delay(void) {
  if (current_trace == TRACE_INVALID)
    return 0.0f;
  int ch = trace[current_trace].channel;
  return current_props._electrical_delay[ch];
}

void set_s21_offset(float offset) {
  if (s21_offset != offset) {
    s21_offset = offset;
    request_to_redraw(REDRAW_MARKER);
  }
}

VNA_SHELL_FUNCTION(cmd_trace) {
  uint32_t t;
  if (argc == 0) {
    for (t = 0; t < TRACES_MAX; t++) {
      if (trace[t].enabled) {
        const char* type = get_trace_typename(trace[t].type, 0);
        const char* channel = get_trace_chname(t);
        float scale = get_trace_scale(t);
        float refpos = get_trace_refpos(t);
        shell_printf("%d %s %s %f %f" VNA_SHELL_NEWLINE_STR, t, type, channel, scale, refpos);
      }
    }
    return;
  }

  if (get_str_index(argv[0], "all") == 0 && argc > 1 && get_str_index(argv[1], "off") == 0) {
    for (t = 0; t < TRACES_MAX; t++)
      set_trace_enable(t, false);
    return;
  }

  t = (uint32_t)my_atoi(argv[0]);
  if (t >= TRACES_MAX)
    goto usage;
  if (argc == 1) {
    const char* type = get_trace_typename(trace[t].type, 0);
    const char* channel = get_trace_chname(t);
    shell_printf("%d %s %s" VNA_SHELL_NEWLINE_STR, t, type, channel);
    return;
  }
  if (get_str_index(argv[1], "off") == 0) {
    set_trace_enable(t, false);
    return;
  }
#if MAX_TRACE_TYPE != 30
#error "Trace type enum possibly changed, check cmd_trace function"
#endif
  // enum TRC_LOGMAG, TRC_PHASE, TRC_DELAY, TRC_SMITH, TRC_POLAR, TRC_LINEAR, TRC_SWR, TRC_REAL,
  // TRC_IMAG, TRC_R, TRC_X, TRC_Z, TRC_ZPHASE,
  //      TRC_G, TRC_B, TRC_Y, TRC_Rp, TRC_Xp, TRC_sC, TRC_sL, TRC_pC, TRC_pL, TRC_Q, TRC_Rser,
  //      TRC_Xser, TRC_Zser, TRC_Rsh, TRC_Xsh, TRC_Zsh, TRC_Qs21
  static const char cmd_type_list[] = "logmag|phase|delay|smith|polar|linear|swr|real|imag|r|x|z|"
                                      "zp|g|b|y|rp|xp|cs|ls|cp|lp|q|rser|xser|zser|rsh|xsh|zsh|q21";
  int type = get_str_index(argv[1], cmd_type_list);
  if (type >= 0) {
    int src = trace[t].channel;
    if (argc > 2) {
      src = my_atoi(argv[2]);
      if ((uint32_t)src > 1)
        goto usage;
    }
    set_trace_type(t, type, src);
    set_trace_enable(t, true);
    return;
  }

  static const char cmd_marker_smith[] =
      "lin|log|ri|rx|rlc|gb|glc|rpxp|rplc|rxsh|rlcsh|rxser|rlcser";
  // Set marker smith format
  int format = get_str_index(argv[1], cmd_marker_smith);
  if (format >= 0) {
    trace[t].smith_format = format;
    return;
  }
  //                                            0      1
  static const char cmd_scale_ref_list[] = "scale|refpos";
  if (argc >= 3) {
    switch (get_str_index(argv[1], cmd_scale_ref_list)) {
    case 0:
      set_trace_scale(t, my_atof(argv[2]));
      break;
    case 1:
      set_trace_refpos(t, my_atof(argv[2]));
      break;
    default:
      goto usage;
    }
  }
  return;
usage:
  shell_printf("trace {0|1|2|3|all} [%s] [src]" VNA_SHELL_NEWLINE_STR
               "trace {0|1|2|3} [%s]" VNA_SHELL_NEWLINE_STR
               "trace {0|1|2|3} {%s} {value}" VNA_SHELL_NEWLINE_STR,
               cmd_type_list, cmd_marker_smith, cmd_scale_ref_list);
}

VNA_SHELL_FUNCTION(cmd_edelay) {
  int ch = 0;
  float value;
  static const char cmd_edelay_list[] = "s11|s21";
  if (argc >= 1) {
    int idx = get_str_index(argv[0], cmd_edelay_list);
    if (idx == -1)
      value = my_atof(argv[0]);
    else {
      ch = idx;
      if (argc != 2)
        goto usage;
      value = my_atof(argv[0]);
    }
    set_electrical_delay(ch, value * 1e-12); // input value in seconds
    return;
  }
usage:
  shell_printf("%f" VNA_SHELL_NEWLINE_STR,
               current_props._electrical_delay[ch] * (1.0f / 1e-12f)); // return in picoseconds
}

VNA_SHELL_FUNCTION(cmd_s21offset) {
  if (argc != 1) {
    shell_printf("%f" VNA_SHELL_NEWLINE_STR, s21_offset); // return in dB
    return;
  }
  set_s21_offset(my_atof(argv[0])); // input value in dB
}

VNA_SHELL_FUNCTION(cmd_marker) {
  static const char cmd_marker_list[] = "on|off";
  int t;
  if (argc == 0) {
    for (t = 0; t < MARKERS_MAX; t++) {
      if (markers[t].enabled) {
        shell_printf("%d %d " VNA_FREQ_FMT_STR "" VNA_SHELL_NEWLINE_STR, t + 1, markers[t].index,
                     markers[t].frequency);
      }
    }
    return;
  }
  request_to_redraw(REDRAW_MARKER | REDRAW_AREA);
  // Marker on|off command
  int enable = get_str_index(argv[0], cmd_marker_list);
  if (enable >= 0) { // string found: 0 - on, 1 - off
    active_marker = enable == 1 ? MARKER_INVALID : 0;
    for (t = 0; t < MARKERS_MAX; t++)
      markers[t].enabled = enable == 0;
    return;
  }

  t = my_atoi(argv[0]) - 1;
  if (t < 0 || t >= MARKERS_MAX)
    goto usage;
  if (argc == 1) {
    shell_printf("%d %d " VNA_FREQ_FMT_STR "" VNA_SHELL_NEWLINE_STR, t + 1, markers[t].index,
                 markers[t].frequency);
    active_marker = t;
    // select active marker
    markers[t].enabled = TRUE;
    return;
  }

  switch (get_str_index(argv[1], cmd_marker_list)) {
  case 0:
    markers[t].enabled = TRUE;
    active_marker = t;
    return;
  case 1:
    markers[t].enabled = FALSE;
    if (active_marker == t)
      active_marker = MARKER_INVALID;
    return;
  default:
    // select active marker and move to index
    markers[t].enabled = TRUE;
    int index = my_atoi(argv[1]);
    set_marker_index(t, index);
    active_marker = t;
    return;
  }
usage:
  shell_printf("marker [n] [%s|{index}]" VNA_SHELL_NEWLINE_STR, cmd_marker_list);
}

VNA_SHELL_FUNCTION(cmd_touchcal) {
  (void)argc;
  (void)argv;
  shell_printf("first touch upper left, then lower right...");
  ui_touch_cal_exec();
  shell_printf("done" VNA_SHELL_NEWLINE_STR "touch cal params: %d %d %d %d" VNA_SHELL_NEWLINE_STR,
               config._touch_cal[0], config._touch_cal[1], config._touch_cal[2],
               config._touch_cal[3]);
  request_to_redraw(REDRAW_ALL);
}

VNA_SHELL_FUNCTION(cmd_touchtest) {
  (void)argc;
  (void)argv;
  ui_touch_draw_test();
}

VNA_SHELL_FUNCTION(cmd_frequencies) {
  int i;
  (void)argc;
  (void)argv;
  for (i = 0; i < sweep_points; i++) {
    shell_printf(VNA_FREQ_FMT_STR VNA_SHELL_NEWLINE_STR, get_frequency(i));
  }
}

#if ENABLE_TRANSFORM_COMMAND
static void set_domain_mode(int mode) // accept DOMAIN_FREQ or DOMAIN_TIME
{
  if (mode != (props_mode & DOMAIN_MODE)) {
    props_mode = (props_mode & ~DOMAIN_MODE) | (mode & DOMAIN_MODE);
    request_to_redraw(REDRAW_FREQUENCY | REDRAW_MARKER);
    lever_mode = LM_MARKER;
  }
}

static inline void set_timedomain_func(
    uint32_t func) // accept TD_FUNC_LOWPASS_IMPULSE, TD_FUNC_LOWPASS_STEP or TD_FUNC_BANDPASS
{
  props_mode = (props_mode & ~TD_FUNC) | func;
}

static inline void
set_timedomain_window(uint32_t func) // accept TD_WINDOW_MINIMUM/TD_WINDOW_NORMAL/TD_WINDOW_MAXIMUM
{
  props_mode = (props_mode & ~TD_WINDOW) | func;
}

VNA_SHELL_FUNCTION(cmd_transform) {
  int i;
  if (argc == 0) {
    goto usage;
  }
  //                                         0   1       2    3        4       5      6       7
  static const char cmd_transform_list[] = "on|off|impulse|step|bandpass|minimum|normal|maximum";
  for (i = 0; i < argc; i++) {
    switch (get_str_index(argv[i], cmd_transform_list)) {
    case 0:
      set_domain_mode(DOMAIN_TIME);
      break;
    case 1:
      set_domain_mode(DOMAIN_FREQ);
      break;
    case 2:
      set_timedomain_func(TD_FUNC_LOWPASS_IMPULSE);
      break;
    case 3:
      set_timedomain_func(TD_FUNC_LOWPASS_STEP);
      break;
    case 4:
      set_timedomain_func(TD_FUNC_BANDPASS);
      break;
    case 5:
      set_timedomain_window(TD_WINDOW_MINIMUM);
      break;
    case 6:
      set_timedomain_window(TD_WINDOW_NORMAL);
      break;
    case 7:
      set_timedomain_window(TD_WINDOW_MAXIMUM);
      break;
    default:
      goto usage;
    }
  }
  return;
usage:
  shell_printf("usage: transform {%s} [...]" VNA_SHELL_NEWLINE_STR, cmd_transform_list);
}
#endif

#if ENABLE_TEST_COMMAND
VNA_SHELL_FUNCTION(cmd_test) {
  (void)argc;
  (void)argv;
}
#endif

#if ENABLE_PORT_COMMAND
VNA_SHELL_FUNCTION(cmd_port) {
  int port;
  if (argc != 1) {
    shell_printf("usage: port {0:TX 1:RX}" VNA_SHELL_NEWLINE_STR);
    return;
  }
  port = my_atoi(argv[0]);
  tlv320aic3204_select(port);
}
#endif

#if ENABLE_STAT_COMMAND
static struct {
  int16_t rms[2];
  int16_t ave[2];
} stat;

VNA_SHELL_FUNCTION(cmd_stat) {
  const int16_t* p = (const int16_t*)sweep_service_rx_buffer();
  int32_t acc0, acc1;
  int32_t ave0, ave1;
  //  float sample[2], ref[2];
  //  minr, maxr,  mins, maxs;
  int32_t count = AUDIO_BUFFER_LEN;
  int i;
  (void)argc;
  (void)argv;
  for (int ch = 0; ch < 2; ch++) {
    tlv320aic3204_select(ch);
    sweep_service_start_capture(4);
    sweep_service_wait_for_capture();
    //    reset_dsp_accumerator();
    //    dsp_process(&p[               0], AUDIO_BUFFER_LEN);
    //    dsp_process(&p[AUDIO_BUFFER_LEN], AUDIO_BUFFER_LEN);

    acc0 = acc1 = 0;
    for (i = 0; i < AUDIO_BUFFER_LEN * 2; i += 2) {
      acc0 += p[i];
      acc1 += p[i + 1];
    }
    ave0 = acc0 / count;
    ave1 = acc1 / count;
    acc0 = acc1 = 0;
    //    minr  = maxr = 0;
    //    mins  = maxs = 0;
    for (i = 0; i < AUDIO_BUFFER_LEN * 2; i += 2) {
      acc0 += (p[i] - ave0) * (p[i] - ave0);
      acc1 += (p[i + 1] - ave1) * (p[i + 1] - ave1);
      //      if (minr < p[i  ]) minr = p[i  ];
      //      if (maxr > p[i  ]) maxr = p[i  ];
      //      if (mins < p[i+1]) mins = p[i+1];
      //      if (maxs > p[i+1]) maxs = p[i+1];
    }
    stat.rms[0] = vna_sqrtf(acc0 / count);
    stat.rms[1] = vna_sqrtf(acc1 / count);
    stat.ave[0] = ave0;
    stat.ave[1] = ave1;
    shell_printf("Ch: %d" VNA_SHELL_NEWLINE_STR, ch);
    shell_printf("average:   r: %6d s: %6d" VNA_SHELL_NEWLINE_STR, stat.ave[0], stat.ave[1]);
    shell_printf("rms:       r: %6d s: %6d" VNA_SHELL_NEWLINE_STR, stat.rms[0], stat.rms[1]);
    //    shell_printf("min:     ref %6d ch %6d" VNA_SHELL_NEWLINE_STR, minr, mins);
    //    shell_printf("max:     ref %6d ch %6d" VNA_SHELL_NEWLINE_STR, maxr, maxs);
  }
  // shell_printf("callback count: %d" VNA_SHELL_NEWLINE_STR, stat.callback_count);
  // shell_printf("interval cycle: %d" VNA_SHELL_NEWLINE_STR, stat.interval_cycles);
  // shell_printf("busy cycle: %d" VNA_SHELL_NEWLINE_STR, stat.busy_cycles);
  // shell_printf("load: %d" VNA_SHELL_NEWLINE_STR, stat.busy_cycles * 100 / stat.interval_cycles);
  //  extern int awd_count;
  //  shell_printf("awd: %d" VNA_SHELL_NEWLINE_STR, awd_count);
}
#endif

#ifndef NANOVNA_VERSION_STRING
#error "NANOVNA_VERSION_STRING must be defined"
#endif

const char NANOVNA_VERSION[] = NANOVNA_VERSION_STRING;

VNA_SHELL_FUNCTION(cmd_version) {
  (void)argc;
  (void)argv;
  shell_printf("%s" VNA_SHELL_NEWLINE_STR, NANOVNA_VERSION);
}

VNA_SHELL_FUNCTION(cmd_vbat) {
  (void)argc;
  (void)argv;
  shell_printf("%d m" S_VOLT VNA_SHELL_NEWLINE_STR, adc_vbat_read());
}

#if ENABLE_VBAT_OFFSET_COMMAND
VNA_SHELL_FUNCTION(cmd_vbat_offset) {
  if (argc != 1) {
    shell_printf("%d" VNA_SHELL_NEWLINE_STR, config._vbat_offset);
    return;
  }
  config._vbat_offset = (int16_t)my_atoi(argv[0]);
  config_service_notify_configuration_changed();
}
#endif

#if ENABLE_SI5351_TIMINGS
VNA_SHELL_FUNCTION(cmd_si5351time) {
  if (argc != 2)
    return;
  int idx = my_atoui(argv[0]);
  uint16_t value = my_atoui(argv[1]);
  si5351_set_timing(idx, value);
}
#endif

#if ENABLE_SI5351_REG_WRITE
VNA_SHELL_FUNCTION(cmd_si5351reg) {

  if (argc != 2) {
    shell_printf("usage: si reg data" VNA_SHELL_NEWLINE_STR);
    return;
  }
  uint8_t reg = my_atoui(argv[0]);
  uint8_t dat = my_atoui(argv[1]);
  uint8_t buf[] = {reg, dat};
  si5351_bulk_write(buf, 2);
}
#endif

#if ENABLE_I2C_TIMINGS
VNA_SHELL_FUNCTION(cmd_i2ctime) {
  if (argc != 4)
    return;
  uint32_t tim = STM32_I2C_TIMINGS(0U, my_atoui(argv[0]), my_atoui(argv[1]), my_atoui(argv[2]),
                                   my_atoui(argv[3]));
  i2c_set_timings(tim);
}
#endif

#if ENABLE_INFO_COMMAND
VNA_SHELL_FUNCTION(cmd_info) {
  (void)argc;
  (void)argv;
  int i = 0;
  while (info_about[i])
    shell_printf("%s" VNA_SHELL_NEWLINE_STR, info_about[i++]);
}
#endif

#if ENABLE_COLOR_COMMAND
VNA_SHELL_FUNCTION(cmd_color) {
  uint32_t color;
  uint16_t i;
  if (argc != 2) {
    shell_printf("usage: color {id} {rgb24}" VNA_SHELL_NEWLINE_STR);
    for (i = 0; i < MAX_PALETTE; i++) {
      color = GET_PALTETTE_COLOR(i);
      color = HEXRGB(color);
      shell_printf(" %2d: 0x%06x" VNA_SHELL_NEWLINE_STR, i, color);
    }
    return;
  }
  i = my_atoui(argv[0]);
  if (i >= MAX_PALETTE)
    return;
  color = RGBHEX(my_atoui(argv[1]));
  config._lcd_palette[i] = color;
  // Redraw all
  request_to_redraw(REDRAW_ALL);
}
#endif

#if ENABLE_I2C_COMMAND
VNA_SHELL_FUNCTION(cmd_i2c) {
  if (argc != 3) {
    shell_printf("usage: i2c page reg data" VNA_SHELL_NEWLINE_STR);
    return;
  }
  uint8_t page = my_atoui(argv[0]);
  uint8_t reg = my_atoui(argv[1]);
  uint8_t data = my_atoui(argv[2]);
  tlv320aic3204_write_reg(page, reg, data);
}
#endif

#if ENABLE_BAND_COMMAND
VNA_SHELL_FUNCTION(cmd_band) {
  static const char cmd_sweep_list[] = "mode|freq|div|mul|omul|pow|opow|l|r|lr|adj";
  if (argc != 3) {
    shell_printf("cmd error" VNA_SHELL_NEWLINE_STR);
    return;
  }
  int idx = my_atoui(argv[0]);
  int pidx = get_str_index(argv[1], cmd_sweep_list);
  si5351_update_band_config(idx, pidx, my_atoui(argv[2]));
}
#endif

#if ENABLE_LCD_COMMAND
VNA_SHELL_FUNCTION(cmd_lcd) {
  uint8_t d[VNA_SHELL_MAX_ARGUMENTS];
  if (argc == 0)
    return;
  for (int i = 0; i < argc; i++)
    d[i] = my_atoui(argv[i]);
  uint32_t ret = lcd_send_register(d[0], argc - 1, &d[1]);
  shell_printf("ret = 0x%08X" VNA_SHELL_NEWLINE_STR, ret);
  chThdSleepMilliseconds(5);
}
#endif

#if ENABLE_THREADS_COMMAND && (CH_CFG_USE_REGISTRY == TRUE)
VNA_SHELL_FUNCTION(cmd_threads) {
  static const char* states[] = {CH_STATE_NAMES};
  thread_t* tp;
  (void)argc;
  (void)argv;
  shell_printf(
      "stklimit|   stack|stk free|    addr|refs|prio|    state|        name" VNA_SHELL_NEWLINE_STR);
  tp = chRegFirstThread();
  do {
    uint32_t max_stack_use = 0U;
#if (CH_DBG_ENABLE_STACK_CHECK == TRUE) || (CH_CFG_USE_DYNAMIC == TRUE)
    uint32_t stklimit = (uint32_t)tp->wabase;
#if CH_DBG_FILL_THREADS == TRUE
    uint8_t* p = (uint8_t*)tp->wabase;
    while (p[max_stack_use] == CH_DBG_STACK_FILL_VALUE)
      max_stack_use++;
#endif
#else
    uint32_t stklimit = 0U;
#endif
    shell_printf("%08x|%08x|%08x|%08x|%4u|%4u|%9s|%12s" VNA_SHELL_NEWLINE_STR, stklimit,
                 (uint32_t)tp->ctx.sp, max_stack_use, (uint32_t)tp, (uint32_t)tp->refs - 1,
                 (uint32_t)tp->prio, states[tp->state], tp->name == NULL ? "" : tp->name);
    tp = chRegNextThread(tp);
  } while (tp != NULL);
}
#endif

#ifdef __USE_SERIAL_CONSOLE__
#if ENABLE_USART_COMMAND
VNA_SHELL_FUNCTION(cmd_usart_cfg) {
  if (argc != 1) {
    //    shell_printf("usage: %s" VNA_SHELL_NEWLINE_STR "current: %u" VNA_SHELL_NEWLINE_STR,
    //    "usart_cfg {baudrate}", config._serial_speed);
    shell_printf("Serial: %u baud" VNA_SHELL_NEWLINE_STR, config._serial_speed);
    return;
  }
  uint32_t speed = my_atoui(argv[0]);
  if (speed < 300)
    speed = 300;
  shell_update_speed(speed);
}

VNA_SHELL_FUNCTION(cmd_usart) {
  uint32_t time = MS2ST(200); // 200ms wait answer by default
  if (argc == 0 || argc > 2 || VNA_MODE(VNA_MODE_CONNECTION))
    return; // Not work in serial mode
  if (argc == 2)
    time = MS2ST(my_atoui(argv[1]));
  sdWriteTimeout(&SD1, (uint8_t*)argv[0], strlen(argv[0]), time);
  sdWriteTimeout(&SD1, (uint8_t*)VNA_SHELL_NEWLINE_STR, sizeof(VNA_SHELL_NEWLINE_STR) - 1, time);
  uint32_t size;
  uint8_t buffer[64];
  while ((size = sdReadTimeout(&SD1, buffer, sizeof(buffer), time)))
    streamWrite(&SDU1, buffer, size);
}
#endif
#endif

#ifdef __REMOTE_DESKTOP__
void send_region(remote_region_t* rd, uint8_t* buf, uint16_t size) {
  if (SDU1.config->usbp->state == USB_ACTIVE) {
    shell_stream_write(rd, sizeof(remote_region_t));
    shell_stream_write(buf, size);
    shell_stream_write(VNA_SHELL_PROMPT_STR VNA_SHELL_NEWLINE_STR, 6);
  } else
    sweep_mode &= ~SWEEP_REMOTE;
}

VNA_SHELL_FUNCTION(cmd_refresh) {
  static const char cmd_enable_list[] = "on|off";
  if (argc != 1)
    return;
  int enable = get_str_index(argv[0], cmd_enable_list);
  if (enable == 0)
    sweep_mode |= SWEEP_REMOTE;
  else if (enable == 1)
    sweep_mode &= ~SWEEP_REMOTE;
  // redraw all on screen
  request_to_redraw(REDRAW_FREQUENCY | REDRAW_CAL_STATUS | REDRAW_AREA | REDRAW_BATTERY);
}

VNA_SHELL_FUNCTION(cmd_touch) {
  if (argc != 2)
    return;
  remote_touch_set(REMOTE_PRESS, my_atoi(argv[0]), my_atoi(argv[1]));
}

VNA_SHELL_FUNCTION(cmd_release) {
  int16_t x = -1, y = -1;
  if (argc == 2) {
    x = my_atoi(argv[0]);
    y = my_atoi(argv[1]);
  }
  remote_touch_set(REMOTE_RELEASE, x, y);
}
#endif

// All SD card shell commands were removed with the filesystem support.

//=============================================================================
VNA_SHELL_FUNCTION(cmd_help);

static const VNAShellCommand commands[] = {
    {"scan", cmd_scan, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP},
#if ENABLE_SCANBIN_COMMAND
    {"scan_bin", cmd_scan_bin, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP},
#endif
    {"data", cmd_data, 0},
    {"frequencies", cmd_frequencies, 0},
    {"freq", cmd_freq, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
    {"sweep", cmd_sweep, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
    {"power", cmd_power, CMD_RUN_IN_LOAD},
#ifdef USE_VARIABLE_OFFSET
    {"offset", cmd_offset, CMD_WAIT_MUTEX | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
#endif
    {"bandwidth", cmd_bandwidth, CMD_RUN_IN_LOAD},
#ifdef __USE_RTC__
    {"time", cmd_time, CMD_RUN_IN_UI},
#endif
#ifdef __VNA_ENABLE_DAC__
    {"dac", cmd_dac, CMD_RUN_IN_LOAD},
#endif
    {"saveconfig", cmd_saveconfig, CMD_RUN_IN_LOAD},
    {"clearconfig", cmd_clearconfig, CMD_RUN_IN_LOAD},
#if ENABLED_DUMP_COMMAND
    {"dump", cmd_dump, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP},
#endif
#if ENABLE_PORT_COMMAND
    {"port", cmd_port, CMD_RUN_IN_LOAD},
#endif
#if ENABLE_STAT_COMMAND
    {"stat", cmd_stat, CMD_WAIT_MUTEX},
#endif
#if ENABLE_GAIN_COMMAND
    {"gain", cmd_gain, CMD_WAIT_MUTEX},
#endif
#if ENABLE_SAMPLE_COMMAND
    {"sample", cmd_sample, 0},
#endif
#if ENABLE_TEST_COMMAND
    {"test", cmd_test, 0},
#endif
    {"touchcal", cmd_touchcal, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP},
    {"touchtest", cmd_touchtest, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP},
    {"pause", cmd_pause, CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
    {"resume", cmd_resume, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
    {"cal", cmd_cal, CMD_WAIT_MUTEX},
    {"save", cmd_save, CMD_RUN_IN_LOAD},
    {"recall", cmd_recall, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
    {"trace", cmd_trace, CMD_RUN_IN_LOAD},
    {"marker", cmd_marker, CMD_RUN_IN_LOAD},
    {"edelay", cmd_edelay, CMD_RUN_IN_LOAD},
    {"s21offset", cmd_s21offset, CMD_RUN_IN_LOAD},
    {"capture", cmd_capture, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI},
#ifdef __VNA_MEASURE_MODULE__
    {"measure", cmd_measure, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
#endif
#ifdef __REMOTE_DESKTOP__
    {"refresh", cmd_refresh, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI},
    {"touch", cmd_touch, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI},
    {"release", cmd_release, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI},
#endif
    {"vbat", cmd_vbat, CMD_RUN_IN_LOAD},
    {"tcxo", cmd_tcxo, CMD_RUN_IN_LOAD},
    {"reset", cmd_reset, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_LOAD},
#ifdef __USE_SMOOTH__
    {"smooth", cmd_smooth, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
#endif
#if ENABLE_CONFIG_COMMAND
    {"config", cmd_config, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
#endif
#ifdef __USE_SERIAL_CONSOLE__
#if ENABLE_USART_COMMAND
    {"usart_cfg", cmd_usart_cfg,
     CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
    {"usart", cmd_usart, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
#endif
#endif
#if ENABLE_VBAT_OFFSET_COMMAND
    {"vbat_offset", cmd_vbat_offset, CMD_RUN_IN_LOAD},
#endif
#if ENABLE_TRANSFORM_COMMAND
    {"transform", cmd_transform, CMD_RUN_IN_LOAD},
#endif
    {"threshold", cmd_threshold, CMD_RUN_IN_LOAD},
    {"help", cmd_help, 0},
#if ENABLE_INFO_COMMAND
    {"info", cmd_info, 0},
#endif
    {"version", cmd_version, 0},
#if ENABLE_COLOR_COMMAND
    {"color", cmd_color, CMD_RUN_IN_LOAD},
#endif
#if ENABLE_I2C_COMMAND
    {"i2c", cmd_i2c, CMD_WAIT_MUTEX},
#endif
#if ENABLE_SI5351_REG_WRITE
    {"si", cmd_si5351reg, CMD_WAIT_MUTEX},
#endif
#if ENABLE_LCD_COMMAND
    {"lcd", cmd_lcd, CMD_WAIT_MUTEX},
#endif
#if ENABLE_THREADS_COMMAND && (CH_CFG_USE_REGISTRY == TRUE)
    {"threads", cmd_threads, 0},
#endif
#if ENABLE_SI5351_TIMINGS
    {"t", cmd_si5351time, CMD_WAIT_MUTEX},
#endif
#if ENABLE_I2C_TIMINGS
    {"i", cmd_i2ctime, CMD_WAIT_MUTEX},
#endif
#if ENABLE_BAND_COMMAND
    {"b", cmd_band, CMD_WAIT_MUTEX},
#endif
    {NULL, NULL, 0}};

VNA_SHELL_FUNCTION(cmd_help) {
  (void)argc;
  (void)argv;
  const VNAShellCommand* scp = commands;
  shell_printf("Commands:");
  while (scp->sc_name != NULL) {
    shell_printf(" %s", scp->sc_name);
    scp++;
  }
  shell_printf(VNA_SHELL_NEWLINE_STR);
  return;
}

/*
 * VNA shell functions
 */

//
// Parse and run command line
//
static void vna_shell_execute_line(char* line) {
  DEBUG_LOG(0, line); // debug console log
  uint16_t argc = 0;
  char** argv = NULL;
  const char* command_name = NULL;
  const VNAShellCommand* cmd = shell_parse_command(line, &argc, &argv, &command_name);
  if (cmd) {
    uint16_t cmd_flag = cmd->flags;
    if ((cmd_flag & CMD_RUN_IN_UI) && (sweep_mode & SWEEP_UI_MODE)) {
      cmd_flag &= (uint16_t)~CMD_WAIT_MUTEX;
    }
    if (cmd_flag & CMD_BREAK_SWEEP) {
      operation_requested |= OP_CONSOLE;
    }
    if (cmd_flag & CMD_WAIT_MUTEX) {
      shell_request_deferred_execution(cmd, argc, argv);
    } else {
      cmd->sc_function((int)argc, argv);
    }
  } else if (command_name && *command_name) {
    shell_printf("%s?" VNA_SHELL_NEWLINE_STR, command_name);
  }
}

#ifdef VNA_SHELL_THREAD
static THD_WORKING_AREA(waThread2, /* cmd_* max stack size + alpha */ 442);
THD_FUNCTION(myshellThread, p) {
  (void)p;
  chRegSetThreadName("shell");
  while (true) {
    shell_printf(VNA_SHELL_PROMPT_STR);
    if (vna_shell_read_line(shell_line, VNA_SHELL_MAX_LENGTH))
      vna_shell_execute_line(shell_line);
    else // Putting a delay in order to avoid an endless loop trying to read an unavailable stream.
      chThdSleepMilliseconds(100);
  }
}
#endif

// Main thread stack size defined in makefile USE_PROCESS_STACKSIZE = 0x200
// Profile stack usage (enable threads command by def ENABLE_THREADS_COMMAND) show:
// Stack maximum usage = 472 bytes (need test more and run all commands), free stack = 40 bytes
//
int app_main(void) {
  /*
   * Initialize ChibiOS systems
   */
  halInit();
  chSysInit();

  platform_init();
  const PlatformDrivers* drivers = platform_get_drivers();
  if (drivers != NULL) {
    if (drivers->init) {
      drivers->init();
    }
    if (drivers->display && drivers->display->init) {
      drivers->display->init();
    }
    if (drivers->adc && drivers->adc->init) {
      drivers->adc->init();
    }
    if (drivers->generator && drivers->generator->init) {
      drivers->generator->init();
    }
    if (drivers->storage && drivers->storage->init) {
      drivers->storage->init();
    }
  }

  measurement_pipeline_init(&measurement_pipeline, drivers);
  sweep_service_init();

  config_service_init();
  event_bus_init(&app_event_bus, app_event_slots, ARRAY_COUNT(app_event_slots),
                 app_event_queue_storage, ARRAY_COUNT(app_event_queue_storage),
                 app_event_nodes, ARRAY_COUNT(app_event_nodes));
  config_service_attach_event_bus(&app_event_bus);
  shell_attach_event_bus(&app_event_bus);

  /*
   * restore config and calibration 0 slot from flash memory, also if need use backup data
   */
  load_settings();
  app_force_resume_sweep();

#ifdef USE_VARIABLE_OFFSET
  si5351_set_frequency_offset(IF_OFFSET);
#endif
  /*
   * Init Shell console connection data
   */
  shell_register_commands(commands);
  shell_init_connection();

  /*
   * tlv320aic Initialize (audio codec)
   */
  tlv320aic3204_init();
  chThdSleepMilliseconds(200); // Wait for aic codec start
                               /*
                                * I2S Initialize
                                */
  init_i2s((void*)sweep_service_rx_buffer(),
           (AUDIO_BUFFER_LEN * 2) * sizeof(audio_sample_t) / sizeof(int16_t));

/*
 * SD Card init (if inserted) allow fix issues
 * Some card after insert work in SDIO mode and can corrupt SPI exchange (need switch it to SPI)
 */
#ifdef __USE_SD_CARD__
  disk_initialize(0);
#endif

  /*
   * I2C bus run on work speed
   */
  i2c_set_timings(STM32_I2C_TIMINGR);

  /*
   * Startup sweep thread
   */
  chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO - 1, Thread1, NULL);

  while (1) {
    if (shell_check_connect()) {
      shell_printf(VNA_SHELL_NEWLINE_STR "NanoVNA Shell" VNA_SHELL_NEWLINE_STR);
#ifdef VNA_SHELL_THREAD
#if CH_CFG_USE_WAITEXIT == FALSE
#error "VNA_SHELL_THREAD use chThdWait, need enable CH_CFG_USE_WAITEXIT in chconf.h"
#endif
      thread_t* shelltp =
          chThdCreateStatic(waThread2, sizeof(waThread2), NORMALPRIO + 1, myshellThread, NULL);
      chThdWait(shelltp);
#else
      do {
        shell_printf(VNA_SHELL_PROMPT_STR);
        if (vna_shell_read_line(shell_line, VNA_SHELL_MAX_LENGTH))
          vna_shell_execute_line(shell_line);
        else
          chThdSleepMilliseconds(200);
      } while (shell_check_connect());
#endif
    }
    chThdSleepMilliseconds(1000);
  }
}

/* The prototype shows it is a naked function - in effect this is just an
assembly function. */
void HardFault_Handler(void);

typedef struct {
  uint32_t r4;
  uint32_t r5;
  uint32_t r6;
  uint32_t r7;
  uint32_t r8;
  uint32_t r9;
  uint32_t r10;
  uint32_t r11;
} hard_fault_extra_registers_t;

__attribute__((noreturn)) void
hard_fault_handler_c(uint32_t* sp, const hard_fault_extra_registers_t* extra, uint32_t exc_return);

__attribute__((naked)) void HardFault_Handler(void) {
  __asm volatile("mov r2, lr\n"
                 "movs r3, #4\n"
                 "tst r3, r2\n"
                 "beq 1f\n"
                 "mrs r0, psp\n"
                 "b 2f\n"
                 "1:\n"
                 "mrs r0, msp\n"
                 "2:\n"
                 "sub sp, #32\n"
                 "mov r1, sp\n"
                 "stmia r1!, {r4-r7}\n"
                 "mov r3, r8\n"
                 "str r3, [r1, #0]\n"
                 "mov r3, r9\n"
                 "str r3, [r1, #4]\n"
                 "mov r3, r10\n"
                 "str r3, [r1, #8]\n"
                 "mov r3, r11\n"
                 "str r3, [r1, #12]\n"
                 "mov r1, sp\n"
                 "bl hard_fault_handler_c\n"
                 "add sp, #32\n"
                 "b .\n");
}

void hard_fault_handler_c(uint32_t* sp, const hard_fault_extra_registers_t* extra,
                          uint32_t exc_return) {
#if ENABLE_HARD_FAULT_HANDLER_DEBUG
  uint32_t r0 = sp[0];
  uint32_t r1 = sp[1];
  uint32_t r2 = sp[2];
  uint32_t r3 = sp[3];
  uint32_t r12 = sp[4];
  uint32_t lr = sp[5];
  uint32_t pc = sp[6];
  uint32_t psr = sp[7];
  int y = 0;
  int x = 20;
  lcd_set_colors(LCD_FG_COLOR, LCD_BG_COLOR);
  lcd_printf(x, y += FONT_STR_HEIGHT, "SP  0x%08x", (uint32_t)sp);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R0  0x%08x", r0);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R1  0x%08x", r1);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R2  0x%08x", r2);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R3  0x%08x", r3);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R4  0x%08x", extra->r4);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R5  0x%08x", extra->r5);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R6  0x%08x", extra->r6);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R7  0x%08x", extra->r7);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R8  0x%08x", extra->r8);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R9  0x%08x", extra->r9);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R10 0x%08x", extra->r10);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R11 0x%08x", extra->r11);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R12 0x%08x", r12);
  lcd_printf(x, y += FONT_STR_HEIGHT, "LR  0x%08x", lr);
  lcd_printf(x, y += FONT_STR_HEIGHT, "PC  0x%08x", pc);
  lcd_printf(x, y += FONT_STR_HEIGHT, "PSR 0x%08x", psr);
  lcd_printf(x, y += FONT_STR_HEIGHT, "EXC 0x%08x", exc_return);

  shell_printf("===================================" VNA_SHELL_NEWLINE_STR);
#else
  (void)sp;
  (void)extra;
  (void)exc_return;
#endif
  while (true) {
  }
}
// For new compilers
// void _exit(int x){(void)x;}
// void _kill(void){}
// int  _write (int file, char *data, int len) {(void)file; (void)data; return len;}
// void _getpid(void){}
#if ENABLED_DUMP_COMMAND
VNA_SHELL_FUNCTION(cmd_dump) {
  int i, j;
  audio_sample_t dump[96 * 2];
  int selection = 0;
  if (argc == 1) {
    selection = (my_atoui(argv[0]) == 1) ? 0 : 1;
  }
  sweep_service_prepare_dump(dump, ARRAY_COUNT(dump), selection);
  tlv320aic3204_select(0);
  sweep_service_start_capture(DELAY_SWEEP_START);
  while (!sweep_service_dump_ready()) {
    __WFI();
  }
  for (i = 0, j = 0; i < (int)ARRAY_COUNT(dump); i++) {
    shell_printf("%6d ", dump[i]);
    if (++j == 12) {
      shell_printf(VNA_SHELL_NEWLINE_STR);
      j = 0;
    }
  }
}
#endif
