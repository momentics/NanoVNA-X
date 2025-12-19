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

#include "interfaces/cli/shell_commands.h"
#include "runtime/runtime_features.h"
#include "nanovna.h"
#include "platform/boards/stm32_peripherals.h"
#include "platform/peripherals/si5351.h"

#include "ui/ui_style.h"
#include "ui/core/ui_core.h"
#include "processing/calibration.h"
#include "rf/sweep/sweep_orchestrator.h"
#include "infra/storage/config_service.h"
#include "infra/state/state_manager.h"
#include "interfaces/ports/ui_port.h"
#include "interfaces/ports/processing_port.h"
#include "interfaces/ports/usb_command_server_port.h"
#include "version_info.h"
#include "runtime/runtime_entry.h" // For globals if needed, but nanovna.h should suffice
#include <string.h>
#include <stdlib.h>

#define VNA_SHELL_FUNCTION(command_name) static __attribute__((unused)) void command_name(int argc __attribute__((unused)), char* argv[] __attribute__((unused)))
#define VNA_FREQ_FMT_STR "%u"
#if defined(NANOVNA_F303)
#define CLI_USAGE_ENABLED 1
#else
#define CLI_USAGE_ENABLED 1
#endif

#if CLI_USAGE_ENABLED
#define PRINT_USAGE(...) shell_printf(__VA_ARGS__)
#else
#define PRINT_USAGE(...) do { (void)0; } while (0)
#endif

// Macros from runtime_entry.c
#define CLI_PRINT_USAGE PRINT_USAGE
#define VNA_SHELL_NEWLINE_STR "\r\n"

#define MAX_BANDWIDTH (AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT)
#define MIN_BANDWIDTH ((AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT) / 512 + 1)

// Helper for set_power (moved from runtime_entry.c)
void set_power(uint8_t value) {
  request_to_redraw(REDRAW_CAL_STATUS);
  if (value > SI5351_CLK_DRIVE_STRENGTH_8MA)
    value = SI5351_CLK_DRIVE_STRENGTH_AUTO;
  if (current_props._power == value)
    return;
  current_props._power = value;
  // Update power if pause, need for generation in CW mode (legacy behaviour)
  if (!(sweep_mode & SWEEP_ENABLE))
    si5351_set_power(value);
}



// Forward declarations
static void cmd_dump(int argc, char* argv[]);
static void cmd_scan(int argc, char* argv[]);
static void cmd_scan_bin(int argc, char* argv[]);
static void cmd_data(int argc, char* argv[]);
static void cmd_frequencies(int argc, char* argv[]);
static void cmd_freq(int argc, char* argv[]);
static void cmd_sweep(int argc, char* argv[]);
static void cmd_power(int argc, char* argv[]);
static void cmd_offset(int argc, char* argv[]);
static void cmd_bandwidth(int argc, char* argv[]);
static void cmd_time(int argc, char* argv[]);
static void cmd_dac(int argc, char* argv[]);
static void cmd_saveconfig(int argc, char* argv[]);
static void cmd_clearconfig(int argc, char* argv[]);
static void cmd_sd_list(int argc, char* argv[]);
static void cmd_sd_read(int argc, char* argv[]);
static void cmd_sd_delete(int argc, char* argv[]);
static void cmd_port(int argc, char* argv[]);
static void cmd_stat(int argc, char* argv[]);
static void cmd_gain(int argc, char* argv[]);
static void cmd_sample(int argc, char* argv[]);
static void cmd_test(int argc, char* argv[]);
static void cmd_touchcal(int argc, char* argv[]);
static void cmd_touchtest(int argc, char* argv[]);
static void cmd_pause(int argc, char* argv[]);
static void cmd_resume(int argc, char* argv[]);
static void cmd_msg(int argc, char* argv[]);
static void cmd_cal(int argc, char* argv[]);
static void cmd_save(int argc, char* argv[]);
static void cmd_recall(int argc, char* argv[]);
static void cmd_trace(int argc, char* argv[]);
static void cmd_marker(int argc, char* argv[]);
static void cmd_edelay(int argc, char* argv[]);
static void cmd_s21offset(int argc, char* argv[]);
static void cmd_capture(int argc, char* argv[]);
static void cmd_measure(int argc, char* argv[]);
static void cmd_refresh(int argc, char* argv[]);
static void cmd_touch(int argc, char* argv[]);
static void cmd_release(int argc, char* argv[]);
static void cmd_vbat(int argc, char* argv[]);
static void cmd_tcxo(int argc, char* argv[]);
static void cmd_reset(int argc, char* argv[]);
static void cmd_smooth(int argc, char* argv[]);
static void cmd_config(int argc, char* argv[]);
static void cmd_usart_cfg(int argc, char* argv[]);
static void cmd_usart(int argc, char* argv[]);
static void cmd_vbat_offset(int argc, char* argv[]);
static void cmd_transform(int argc, char* argv[]);
static void cmd_threshold(int argc, char* argv[]);
static void cmd_help(int argc, char* argv[]);
static void cmd_info(int argc, char* argv[]);
static void cmd_version(int argc, char* argv[]);
static void cmd_color(int argc, char* argv[]);
static void cmd_i2c(int argc, char* argv[]);
static void cmd_si5351reg(int argc, char* argv[]);
static void cmd_lcd(int argc, char* argv[]);
static void cmd_threads(int argc, char* argv[]);
static void cmd_si5351time(int argc, char* argv[]);
static void cmd_i2ctime(int argc, char* argv[]);
static void cmd_band(int argc, char* argv[]);

// Implementations

VNA_SHELL_FUNCTION(cmd_power) {
  if (argc != 1) {
    CLI_PRINT_USAGE("usage: power {0-3}|{255 - auto}" VNA_SHELL_NEWLINE_STR
                    "power: %d" VNA_SHELL_NEWLINE_STR,
                    current_props._power);
    return;
  }
  if (get_str_index(argv[0], "auto") == 0) {
    set_power(SI5351_CLK_DRIVE_STRENGTH_AUTO);
    return;
  }
  set_power(my_atoi(argv[0]));
}

VNA_SHELL_FUNCTION(cmd_offset) {
#ifdef USE_VARIABLE_OFFSET
  if (argc != 1) {
    CLI_PRINT_USAGE("usage: %s" VNA_SHELL_NEWLINE_STR "current: %u" VNA_SHELL_NEWLINE_STR,
                    "offset {frequency offset(Hz)}", IF_OFFSET);
    return;
  }
  int32_t requested = my_atoi(argv[0]);
  int32_t clamped = clamp_if_offset(requested);
  if (clamped != requested) {
    shell_printf("offset clamped to %ld Hz" VNA_SHELL_NEWLINE_STR, (long)clamped);
  }
  si5351_set_frequency_offset(clamped);
  config_service_notify_configuration_changed();
#endif
}

VNA_SHELL_FUNCTION(cmd_time) {
#ifdef __USE_RTC__
  uint32_t dt_buf[2];
  dt_buf[0] = rtc_get_tr_bcd(); // TR should be read first for sync
  dt_buf[1] = rtc_get_dr_bcd(); // DR should be read second
  static const uint8_t idx_to_time[] = {6, 5, 4, 2, 1, 0};
  static const char time_cmd[] = "y|m|d|h|min|sec|ppm";
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
  time[idx_to_time[idx]] = ((val / 10) << 4) | (val % 10);
  rtc_set_time(dt_buf[1], dt_buf[0]);
  return;
usage:
  shell_printf("20%02x/%02x/%02x %02x:%02x:%02x" VNA_SHELL_NEWLINE_STR
               "usage: time {[%s] 0-99} or {b 0xYYMMDD 0xHHMMSS}" VNA_SHELL_NEWLINE_STR,
               time[6], time[5], time[4], time[2], time[1], time[0], time_cmd);
#else
  (void)argc;
  (void)argv;
#endif
}

VNA_SHELL_FUNCTION(cmd_dac) {
#ifdef __VNA_ENABLE_DAC__
  if (argc != 1) return;
  // si5351_dvc_write(my_atoui(argv[0])); // Assuming this function exists or similar
#endif
}

VNA_SHELL_FUNCTION(cmd_measure) {
#ifdef __VNA_MEASURE_MODULE__
  static const char cmd_measure_list[] = "none"
#ifdef __USE_LC_MATCHING__
                                         "|lc"
#endif
#ifdef __S21_MEASURE__
                                         "|lcshunt"
                                         "|lcseries"
                                         "|xtal"
                                         "|filter"
#endif
#ifdef __S11_CABLE_MEASURE__
                                         "|cable"
#endif
#ifdef __S11_RESONANCE_MEASURE__
                                         "|resonance"
#endif
      ;
  int idx;
  if (argc == 1 && (idx = get_str_index(argv[0], cmd_measure_list)) >= 0)
    plot_set_measure_mode((uint8_t)idx);
  else
    CLI_PRINT_USAGE("usage: measure {%s}" VNA_SHELL_NEWLINE_STR, cmd_measure_list);
#endif
}

// Copied from shell_commands.c (previous)
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
  static freq_t original_start;
  static freq_t original_stop;
  static uint16_t original_points;
  original_start = get_sweep_frequency(ST_START);
  original_stop = get_sweep_frequency(ST_STOP);
  original_points = sweep_points;
  bool restore_config = false;

  if (argc < 2 || argc > 4) {
    PRINT_USAGE("usage: scan {start(Hz)} {stop(Hz)} [points] [outmask]" VNA_SHELL_NEWLINE_STR);
    return;
  }

  start = my_atoui(argv[0]);
  stop = my_atoui(argv[1]);
  // Validate frequency range: 50kHz to 2.7GHz
  if (start == 0 || stop == 0 || start > stop || start < 50000 || stop > FREQUENCY_MAX) {
    if (start < 50000 || start > FREQUENCY_MAX) {
      shell_printf("start frequency out of range (50kHz-2.7GHz): %lu Hz" VNA_SHELL_NEWLINE_STR,
                   (unsigned long)start);
    } else if (stop < 50000 || stop > FREQUENCY_MAX) {
      shell_printf("stop frequency out of range (50kHz-2.7GHz): %lu Hz" VNA_SHELL_NEWLINE_STR,
                   (unsigned long)stop);
    } else {
      shell_printf("frequency range is invalid" VNA_SHELL_NEWLINE_STR);
    }
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
    sweep_points = points;
    if (points != original_points)
      restore_config = true;
  }
  uint16_t mask = 0;
  uint16_t sweep_ch = SWEEP_CH0_MEASURE | SWEEP_CH1_MEASURE;

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

  sweep_points = points;
  app_measurement_set_frequencies(start, stop, points);

  if (sweep_ch & (SWEEP_CH0_MEASURE | SWEEP_CH1_MEASURE)) {
    app_measurement_reset();
    app_measurement_sweep(false, sweep_ch);
  }
  pause_sweep();
  
  if (mask) {
    if (mask & SCAN_MASK_BINARY) {
      shell_stream_write(&mask, sizeof(uint16_t));
      shell_stream_write(&points, sizeof(uint16_t));
      for (int i = 0; i < points; i++) {
        if (mask & SCAN_MASK_OUT_FREQ) {
          freq_t f = get_frequency(i);
          shell_stream_write(&f, sizeof(freq_t));
        }
        if (mask & SCAN_MASK_OUT_DATA0)
          shell_stream_write(&measured[0][i][0], sizeof(float) * 2);
        if (mask & SCAN_MASK_OUT_DATA1)
          shell_stream_write(&measured[1][i][0], sizeof(float) * 2);
      }
    } else {
      for (int i = 0; i < points; i++) {
        if (mask & SCAN_MASK_OUT_FREQ)
          if (!shell_printf(VNA_FREQ_FMT_STR " ", get_frequency(i))) break;
        if (mask & SCAN_MASK_OUT_DATA0)
          if (!shell_printf("%f %f ", measured[0][i][0], measured[0][i][1])) break;
        if (mask & SCAN_MASK_OUT_DATA1)
          if (!shell_printf("%f %f ", measured[1][i][0], measured[1][i][1])) break;
        if (!shell_printf(VNA_SHELL_NEWLINE_STR)) break;
      }
    }
  }

  if (restore_config) {
    sweep_points = original_points;
    app_measurement_update_frequencies();
  }
  resume_sweep();
}

VNA_SHELL_FUNCTION(cmd_scan_bin) {
#if ENABLE_SCANBIN_COMMAND
  sweep_mode |= SWEEP_BINARY;
  cmd_scan(argc, argv);
  sweep_mode &= ~(SWEEP_BINARY);
#endif
}

VNA_SHELL_FUNCTION(cmd_bandwidth) {
  uint16_t user_bw;
  if (argc == 1)
    user_bw = my_atoui(argv[0]);
  else if (argc == 2) {
    /* Accept bandwidth in Hz and translate to register value like legacy firmware */
    uint16_t f = my_atoui(argv[0]);
    if (f > MAX_BANDWIDTH)
      user_bw = 0;
    else if (f < MIN_BANDWIDTH)
      user_bw = 511;
    else
      user_bw = ((AUDIO_ADC_FREQ + AUDIO_SAMPLES_COUNT / 2) / AUDIO_SAMPLES_COUNT) / f - 1;
  } else {
    shell_printf("bandwidth %d (%uHz)" VNA_SHELL_NEWLINE_STR, config._bandwidth,
                 get_bandwidth_frequency(config._bandwidth));
    return;
  }
  set_bandwidth(user_bw);
  shell_printf("bandwidth %d (%uHz)" VNA_SHELL_NEWLINE_STR, config._bandwidth,
               get_bandwidth_frequency(config._bandwidth));
}

VNA_SHELL_FUNCTION(cmd_freq) {
  if (argc != 1) {
    CLI_PRINT_USAGE("usage: freq {frequency(Hz)}" VNA_SHELL_NEWLINE_STR);
    return;
  }
  uint32_t freq = my_atoui(argv[0]);
  if (freq < 50000 || freq > FREQUENCY_MAX) {
    shell_printf("error: frequency out of range (50kHz-2.7GHz): %lu Hz" VNA_SHELL_NEWLINE_STR,
                 (unsigned long)freq);
    return;
  }
  pause_sweep();
  app_measurement_set_frequency(freq);
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
  static const char sweep_cmd[] = "start|stop|center|span|cw|step|var";
  if (argc == 2 && value0 == 0) {
    int type = get_str_index(argv[0], sweep_cmd);
    if (type == -1)
      goto usage;
    bool enforce = !(type == ST_START || type == ST_STOP);
    if ((value1 < 50000 || value1 > FREQUENCY_MAX)) {
      shell_printf("error: frequency out of range (50kHz-2.7GHz): %lu Hz" VNA_SHELL_NEWLINE_STR,
                   (unsigned long)value1);
      return;
    }
    set_sweep_frequency_internal(type, value1, enforce);
    return;
  }
  // Parse sweep {start(Hz)} [stop(Hz)] [points]
  if (value0 && (value0 < 50000 || value0 > FREQUENCY_MAX)) {
    shell_printf("error: start frequency out of range (50kHz-2.7GHz): %lu Hz" VNA_SHELL_NEWLINE_STR,
                 (unsigned long)value0);
    return;
  }
  if (value1 && (value1 < 50000 || value1 > FREQUENCY_MAX)) {
    shell_printf("error: stop frequency out of range (50kHz-2.7GHz): %lu Hz" VNA_SHELL_NEWLINE_STR,
                 (unsigned long)value1);
    return;
  }
  if (value0)
    set_sweep_frequency_internal(ST_START, value0, false);
  if (value1)
    set_sweep_frequency_internal(ST_STOP, value1, false);
  if (value2)
    set_sweep_points(value2);
  return;
usage:
  CLI_PRINT_USAGE("usage: sweep {start(Hz)} [stop(Hz)] [points]" VNA_SHELL_NEWLINE_STR
                  "\tsweep {%s} {freq(Hz)}" VNA_SHELL_NEWLINE_STR,
                  sweep_cmd);
}

VNA_SHELL_FUNCTION(cmd_data) {
  int sel = 0;
  const float (*array)[2];
  uint16_t points = sweep_points; // Default to current sweep points
  
  if (argc == 1) {
    sel = my_atoi(argv[0]);
  }
  if (sel < 0 || sel >= 7) {
    PRINT_USAGE("usage: data [array]" VNA_SHELL_NEWLINE_STR);
    return;
  }

  if (sel < 2) {
      // Use runtime_entry.c logic
    sweep_service_snapshot_t snapshot;
    if (sweep_mode & SWEEP_ENABLE)
        sweep_service_wait_for_generation();
    while (true) {
      if (!sweep_service_snapshot_acquire((uint8_t)sel, &snapshot)) {
        chThdSleepMilliseconds(1);
        continue;
      }
      for (uint16_t i = 0; i < snapshot.points; i++) {
        if (!shell_printf("%f %f" VNA_SHELL_NEWLINE_STR, snapshot.data[i][0], snapshot.data[i][1])) break;
        if ((i & 0x0F) == 0x0F) chThdYield();
      }
      if (sweep_service_snapshot_release(&snapshot)) return;
      chThdYield();
    }
  } else {
    array = cal_data[sel - 2];
    osalSysLock();
    points = cal_sweep_points;
    osalSysUnlock();
  }

  for (uint16_t i = 0; i < points; i++) {
    if (!shell_printf("%f %f" VNA_SHELL_NEWLINE_STR, array[i][0], array[i][1])) break;
    if ((i & 0x0F) == 0x0F) chThdYield();
  }
}

VNA_SHELL_FUNCTION(cmd_threshold) {
  uint32_t value;
  if (argc != 1) {
    PRINT_USAGE("usage: %s" VNA_SHELL_NEWLINE_STR "current: %u" VNA_SHELL_NEWLINE_STR,
                 "threshold {frequency in harmonic mode}", config._harmonic_freq_threshold);
    return;
  }
  uint32_t requested = my_atoui(argv[0]);
  value = clamp_harmonic_threshold(requested);
  if (value != requested) {
    shell_printf("threshold clamped to %u Hz" VNA_SHELL_NEWLINE_STR, value);
  }
  config._harmonic_freq_threshold = value;
  config_service_notify_configuration_changed();
}

VNA_SHELL_FUNCTION(cmd_saveconfig) {
  config_save();
  state_manager_force_save();
  shell_printf("Config saved" VNA_SHELL_NEWLINE_STR);
}

VNA_SHELL_FUNCTION(cmd_clearconfig) {
  if (argc != 1) {
    PRINT_USAGE("usage: clearconfig {protection key}" VNA_SHELL_NEWLINE_STR);
    return;
  }
  if (get_str_index(argv[0], "1234") != 0) {
    shell_printf("Key unmatched." VNA_SHELL_NEWLINE_STR);
    return;
  }
  clear_all_config_prop_data();
  shell_printf("Config and all cal data cleared." VNA_SHELL_NEWLINE_STR
               "Do reset manually to take effect. Then do touch cal and save." VNA_SHELL_NEWLINE_STR);
}

VNA_SHELL_FUNCTION(cmd_capture) {
  // read 2 row pixel time force
#define READ_ROWS 2
  for (int y = 0; y < LCD_HEIGHT; y += READ_ROWS) {
    lcd_read_memory(0, y, LCD_WIDTH, READ_ROWS, (uint16_t*)spi_buffer);
    shell_stream_write(spi_buffer, READ_ROWS * LCD_WIDTH * sizeof(uint16_t));
  }
}

VNA_SHELL_FUNCTION(cmd_sample) {
#if ENABLE_SAMPLE_COMMAND
  if (argc != 1) return;
  static const char cmd_sample_list[] = "gamma|ampl|ref";
  switch (get_str_index(argv[0], cmd_sample_list)) {
  case 0: sweep_service_set_sample_function(processing_port.api->calculate_gamma); return;
  case 1: sweep_service_set_sample_function(processing_port.api->fetch_amplitude); return;
  case 2: sweep_service_set_sample_function(processing_port.api->fetch_amplitude_ref); return;
  }
#endif
}

VNA_SHELL_FUNCTION(cmd_cal) {
  static const char* items[] = {"load", "open", "short", "thru", "isoln", "Es", "Er", "Et", "cal'ed"};
  if (argc == 0) {
    for (int i = 0; i < 9; i++) if (cal_status & (1 << i)) shell_printf("%s ", items[i]);
    shell_printf(VNA_SHELL_NEWLINE_STR);
    return;
  }
  request_to_redraw(REDRAW_CAL_STATUS);
  static const char cmd_cal_list[] = "load|open|short|thru|isoln|done|on|off|reset";
  switch (get_str_index(argv[0], cmd_cal_list)) {
  case 0: cal_collect(CAL_LOAD); return;
  case 1: cal_collect(CAL_OPEN); return;
  case 2: cal_collect(CAL_SHORT); return;
  case 3: cal_collect(CAL_THRU); return;
  case 4: cal_collect(CAL_ISOLN); return;
  case 5: cal_done(); return;
  case 6: cal_status |= CALSTAT_APPLY; return;
  case 7: cal_status &= ~CALSTAT_APPLY; return;
  case 8: cal_status = 0; return;
  }
}

VNA_SHELL_FUNCTION(cmd_save) {
  if (argc != 1) return;
  uint32_t id = my_atoui(argv[0]);
  if (id >= SAVEAREA_MAX) return;
  caldata_save(id);
  request_to_redraw(REDRAW_CAL_STATUS);
}

VNA_SHELL_FUNCTION(cmd_recall) {
  if (argc != 1) return;
  uint32_t id = my_atoui(argv[0]);
  if (id >= SAVEAREA_MAX) return;
  load_properties(id);
}

VNA_SHELL_FUNCTION(cmd_trace) {
  uint32_t t;
  if (argc == 0) {
    for (t = 0; t < TRACES_MAX; t++) {
      if (trace[t].enabled) {
        const char* type = get_trace_typename(trace[t].type, 0);
        const char* channel = get_trace_chname(t);
        shell_printf("%d %s %s %f %f" VNA_SHELL_NEWLINE_STR, t, type, channel, trace[t].scale, trace[t].refpos);
      }
    }
    return;
  }
  t = (uint32_t)my_atoi(argv[0]);
  if (t >= TRACES_MAX) return;
  if (argc >= 2 && get_str_index(argv[1], "off") == 0) {
    set_trace_enable(t, false);
    return;
  }
  static const char cmd_type_list[] = "logmag|phase|delay|smith|polar|linear|swr|real|imag|r|x|z|zp|g|b|y|rp|xp|cs|ls|cp|lp|q|rser|xser|zser|rsh|xsh|zsh|q21";
  int type = get_str_index(argv[1], cmd_type_list);
  if (type >= 0) {
      int src = trace[t].channel;
      set_trace_type(t, type, src);
      set_trace_enable(t, true);
  }
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
  default: {
    markers[t].enabled = TRUE;
    int index = my_atoi(argv[1]);
    set_marker_index(t, index);
    active_marker = t;
    return;
  }
  }
usage:
  shell_printf("marker [n] [%s|{index}]" VNA_SHELL_NEWLINE_STR, cmd_marker_list);
}

VNA_SHELL_FUNCTION(cmd_edelay) {
  int ch = 0;
  float value;
  static const char cmd_edelay_list[] = "s11|s21";
  if (argc >= 1) {
    int idx = get_str_index(argv[0], cmd_edelay_list);
    if (idx == -1) value = my_atof(argv[0]);
    else { ch = idx; if (argc == 2) value = my_atof(argv[0]); else return; }
    set_electrical_delay(ch, value * 1e-12);
    return;
  }
  shell_printf("%f" VNA_SHELL_NEWLINE_STR, current_props._electrical_delay[ch] * (1.0f / 1e-12f));
}

VNA_SHELL_FUNCTION(cmd_s21offset) {
  if (argc != 1) {
    shell_printf("%f" VNA_SHELL_NEWLINE_STR, s21_offset);
    return;
  }
  set_s21_offset(my_atof(argv[0]));
}

VNA_SHELL_FUNCTION(cmd_touchcal) {
  shell_printf("first touch upper left, then lower right...");
  ui_port.api->touch_cal_exec();
  shell_printf("done" VNA_SHELL_NEWLINE_STR "touch cal params: %d %d %d %d" VNA_SHELL_NEWLINE_STR,
               config._touch_cal[0], config._touch_cal[1], config._touch_cal[2],
               config._touch_cal[3]);
  request_to_redraw(REDRAW_ALL);
}

VNA_SHELL_FUNCTION(cmd_touchtest) {
  ui_port.api->touch_draw_test();
}

VNA_SHELL_FUNCTION(cmd_frequencies) {
  for (int i = 0; i < sweep_points; i++) {
    shell_printf(VNA_FREQ_FMT_STR VNA_SHELL_NEWLINE_STR, get_frequency(i));
  }
}


#ifdef ENABLE_TRANSFORM_COMMAND
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
    shell_printf("usage: transform {on|off|impulse|step|bandpass|minimum|normal|maximum}" VNA_SHELL_NEWLINE_STR);
    return;
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
    }
  }
}
#else
VNA_SHELL_FUNCTION(cmd_transform) {}
#endif

VNA_SHELL_FUNCTION(cmd_dump) {
#if ENABLED_DUMP_COMMAND
  int i, j;
  // Use global spi_buffer instead of stack allocation to prevent overflow
  // F303 stack is only 512 bytes, and this buffer needs ~384 bytes
  extern pixel_t spi_buffer[];
  audio_sample_t* dump = (audio_sample_t*)spi_buffer;
  int selection = 0;
  if (argc == 1) selection = (my_atoui(argv[0]) == 1) ? 0 : 1;
  const int dump_count = AUDIO_BUFFER_LEN * 2;
  sweep_service_prepare_dump(dump, dump_count, selection);
  // tlv320aic3204_select(0);
  sweep_service_start_capture(DELAY_SWEEP_START);
  while (!sweep_service_dump_ready()) __WFI();
  for (i = 0, j = 0; i < dump_count; i++) {
    shell_printf("%6d ", dump[i]);
    if (++j == 12) { shell_printf(VNA_SHELL_NEWLINE_STR); j = 0; }
  }
#endif
}

// Commands primarily used by host software during init
VNA_SHELL_FUNCTION(cmd_pause) {
  (void)argc;
  (void)argv;
  pause_sweep();
}

VNA_SHELL_FUNCTION(cmd_resume) {
  (void)argc;
  (void)argv;
  // Restore frequencies array and calibration state (legacy behaviour)
  app_measurement_update_frequencies();
  resume_sweep();
}

VNA_SHELL_FUNCTION(cmd_vbat) {
  (void)argc;
  (void)argv;
  shell_printf("%d m" S_VOLT VNA_SHELL_NEWLINE_STR, adc_vbat_read());
}

VNA_SHELL_FUNCTION(cmd_tcxo) {
  if (argc != 1) {
    CLI_PRINT_USAGE("usage: %s" VNA_SHELL_NEWLINE_STR "current: %u" VNA_SHELL_NEWLINE_STR,
                    "tcxo {TCXO frequency(Hz)}", config._xtal_freq);
    return;
  }
  si5351_set_tcxo(my_atoui(argv[0]));
}

// Remote desktop commands are only available on F303 due to driver support
#if defined(__REMOTE_DESKTOP__) && defined(NANOVNA_F303)
VNA_SHELL_FUNCTION(cmd_refresh) {
  static const char cmd_enable_list[] = "on|off";
  if (argc != 1)
    return;
  int enable = get_str_index(argv[0], cmd_enable_list);
  if (enable == 0)
    sweep_mode |= SWEEP_REMOTE;
  else if (enable == 1)
    sweep_mode &= (uint8_t)~SWEEP_REMOTE;
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
#else
VNA_SHELL_FUNCTION(cmd_refresh) {}
VNA_SHELL_FUNCTION(cmd_touch) {}
VNA_SHELL_FUNCTION(cmd_release) {}
#endif

#if ENABLE_SD_CARD_COMMAND && defined(__USE_SD_CARD__)
static FRESULT cmd_sd_card_mount(void) {
  const FRESULT res = f_mount(filesystem_volume(), "", 1);
  if (res != FR_OK)
    shell_printf("err: no card" VNA_SHELL_NEWLINE_STR);
  return res;
}

VNA_SHELL_FUNCTION(cmd_sd_list) {
  DIR dj;
  FILINFO fno;
  if (cmd_sd_card_mount() != FR_OK)
    return;
  switch (argc) {
  case 0:
    dj.pat = "*.*";
    break;
  case 1:
    dj.pat = argv[0];
    break;
  default:
    CLI_PRINT_USAGE("usage: sd_list {pattern}" VNA_SHELL_NEWLINE_STR);
    return;
  }
  if (f_opendir(&dj, "") == FR_OK) {
    while (f_findnext(&dj, &fno) == FR_OK && fno.fname[0])
      shell_printf("%s %u" VNA_SHELL_NEWLINE_STR, fno.fname, (unsigned)fno.fsize);
  }
  f_closedir(&dj);
}

VNA_SHELL_FUNCTION(cmd_sd_read) {
  char* buf = (char*)spi_buffer;
  if (argc != 1) {
    CLI_PRINT_USAGE("usage: sd_read {filename}" VNA_SHELL_NEWLINE_STR);
    return;
  }
  const char* filename = argv[0];
  if (cmd_sd_card_mount() != FR_OK)
    return;
  FIL* const file = filesystem_file();
  if (f_open(file, filename, FA_OPEN_EXISTING | FA_READ) != FR_OK) {
    shell_printf("err: no file" VNA_SHELL_NEWLINE_STR);
    return;
  }
  uint32_t filesize = f_size(file);
  shell_stream_write(&filesize, 4);
  UINT size = 0;
  while (f_read(file, buf, 512, &size) == FR_OK && size > 0)
    shell_stream_write(buf, size);
  f_close(file);
}

VNA_SHELL_FUNCTION(cmd_sd_delete) {
  if (argc != 1) {
    CLI_PRINT_USAGE("usage: sd_delete {filename}" VNA_SHELL_NEWLINE_STR);
    return;
  }
  if (cmd_sd_card_mount() != FR_OK)
    return;
  const char* filename = argv[0];
  const FRESULT res = f_unlink(filename);
  shell_printf("delete: %s %s" VNA_SHELL_NEWLINE_STR, filename, res == FR_OK ? "OK" : "err");
}
#else
VNA_SHELL_FUNCTION(cmd_sd_list) {}
VNA_SHELL_FUNCTION(cmd_sd_read) {}
VNA_SHELL_FUNCTION(cmd_sd_delete) {}
#endif

VNA_SHELL_FUNCTION(cmd_port) {}
VNA_SHELL_FUNCTION(cmd_stat) {}
VNA_SHELL_FUNCTION(cmd_gain) {}
VNA_SHELL_FUNCTION(cmd_test) {}
#ifdef __SD_CARD_LOAD__
VNA_SHELL_FUNCTION(cmd_msg) {
  if (argc == 0) {
    CLI_PRINT_USAGE("usage: msg delay [text] [header]" VNA_SHELL_NEWLINE_STR);
    return;
  }
  uint32_t delay = my_atoui(argv[0]);
  char *header = 0, *text = 0;
  if (argc > 1)
    text = argv[1];
  if (argc > 2)
    header = argv[2];
  ui_port.api->message_box(header, text, delay);
}
#else
VNA_SHELL_FUNCTION(cmd_msg) {}
#endif

#ifdef __USE_SMOOTH__
VNA_SHELL_FUNCTION(cmd_smooth) {
  if (argc != 1) {
    CLI_PRINT_USAGE("usage: %s" VNA_SHELL_NEWLINE_STR "current: %u" VNA_SHELL_NEWLINE_STR,
                    "smooth {0-8}", get_smooth_factor());
    return;
  }
  set_smooth_factor(my_atoui(argv[0]));
}
#else
VNA_SHELL_FUNCTION(cmd_smooth) {}
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
    const uint32_t value = my_atoui(argv[1]);
    apply_vna_mode((uint16_t)idx, value ? VNA_MODE_SET : VNA_MODE_CLR);
  } else {
    CLI_PRINT_USAGE("usage: config {%s} [0|1]" VNA_SHELL_NEWLINE_STR, cmd_mode_list);
  }
}
#else
VNA_SHELL_FUNCTION(cmd_config) {}
#endif

#ifdef __USE_SERIAL_CONSOLE__
#if ENABLE_USART_COMMAND
VNA_SHELL_FUNCTION(cmd_usart_cfg) {
  if (argc != 1) {
    shell_printf("Serial: %u baud" VNA_SHELL_NEWLINE_STR, config._serial_speed);
    return;
  }
  uint32_t speed = my_atoui(argv[0]);
  if (speed < 300)
    speed = 300;
  shell_update_speed(speed);
}

VNA_SHELL_FUNCTION(cmd_usart) {
  uint32_t time = MS2ST(200);
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
#else
VNA_SHELL_FUNCTION(cmd_usart_cfg) {}
VNA_SHELL_FUNCTION(cmd_usart) {}
#endif
#else
VNA_SHELL_FUNCTION(cmd_usart_cfg) {}
VNA_SHELL_FUNCTION(cmd_usart) {}
#endif

#if ENABLE_VBAT_OFFSET_COMMAND
VNA_SHELL_FUNCTION(cmd_vbat_offset) {
  if (argc != 1) {
    shell_printf("%d" VNA_SHELL_NEWLINE_STR, config._vbat_offset);
    return;
  }
  config._vbat_offset = (int16_t)my_atoi(argv[0]);
}
#else
VNA_SHELL_FUNCTION(cmd_vbat_offset) {}
#endif
VNA_SHELL_FUNCTION(cmd_help) {
  (void)argc;
  (void)argv;
  const vna_shell_command* scp = commands;
  shell_printf("Commands:");
  while (scp->sc_name != NULL) {
    shell_printf(" %s", scp->sc_name);
    scp++;
  }
  shell_printf(VNA_SHELL_NEWLINE_STR);
}
VNA_SHELL_FUNCTION(cmd_info) {
  (void)argc;
  (void)argv;
  int i = 0;
  while (info_about[i]) {
    shell_printf("%s" VNA_SHELL_NEWLINE_STR, info_about[i++]);
  }
}
VNA_SHELL_FUNCTION(cmd_version) { shell_printf("%s" VNA_SHELL_NEWLINE_STR, NANOVNA_VERSION_STRING); }
#if ENABLE_COLOR_COMMAND
VNA_SHELL_FUNCTION(cmd_color) {
  uint32_t color;
  uint16_t i;
  if (argc != 2) {
    CLI_PRINT_USAGE("usage: color {id} {rgb24}" VNA_SHELL_NEWLINE_STR);
    for (i = 0; i < MAX_PALETTE; i++) {
      color = GET_PALTETTE_COLOR(i);
      color = HEXRGB(color);
      shell_printf(" %2d: 0x%06x" VNA_SHELL_NEWLINE_STR, i, (unsigned)color);
    }
    return;
  }
  i = my_atoui(argv[0]);
  if (i >= MAX_PALETTE)
    return;
  color = RGBHEX(my_atoui(argv[1]));
  config._lcd_palette[i] = color;
  request_to_redraw(REDRAW_ALL);
}
#else
VNA_SHELL_FUNCTION(cmd_color) {}
#endif
VNA_SHELL_FUNCTION(cmd_i2c) {} // Keep empty if debug
VNA_SHELL_FUNCTION(cmd_si5351reg) {} // Keep empty if debug
VNA_SHELL_FUNCTION(cmd_lcd) {
    // shell_printf("LCD ID: %04x" VNA_SHELL_NEWLINE_STR, lcd_read_id());
}
VNA_SHELL_FUNCTION(cmd_threads) {
#if defined(VNA_SHELL_THREAD) || 1
    // Requires ChibiOS registry access, skip for now to avoid compile error
    shell_printf("Threads command not fully implemented" VNA_SHELL_NEWLINE_STR);
#endif
}
VNA_SHELL_FUNCTION(cmd_si5351time) {}
VNA_SHELL_FUNCTION(cmd_i2ctime) {}
VNA_SHELL_FUNCTION(cmd_band) {
    if (argc > 0) {
        int band = my_atoi(argv[0]);
        // Implement band switch if applicable
        (void)band;
    }
}
VNA_SHELL_FUNCTION(cmd_reset) {
  (void)argc;
  (void)argv;
#ifdef __DFU_SOFTWARE_MODE__
  if (argc == 1) {
    if (get_str_index(argv[0], "dfu") == 0) {
      shell_printf("Performing reset to DFU mode" VNA_SHELL_NEWLINE_STR);
      ui_port.api->enter_dfu();
      return;
    }
  }
#endif
  shell_printf("Performing reset" VNA_SHELL_NEWLINE_STR);
  NVIC_SystemReset();
}

const vna_shell_command commands[] = {
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
#if ENABLE_SD_CARD_COMMAND
    {"sd_list", cmd_sd_list, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI},
    {"sd_read", cmd_sd_read, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI},
    {"sd_delete", cmd_sd_delete, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI},
#endif
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
    {"pause", cmd_pause, CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD | CMD_NO_AUTO_RESUME},
    {"resume", cmd_resume, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD | CMD_NO_AUTO_RESUME},
#ifdef __SD_CARD_LOAD__
    {"msg", cmd_msg, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_LOAD},
#endif
    {"cal", cmd_cal, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP},
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
    {"reset", cmd_reset, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_LOAD | CMD_NO_AUTO_RESUME},
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
#if ENABLE_LCD_COMMAND
    {"lcd", cmd_lcd, CMD_WAIT_MUTEX},
#endif
#if ENABLE_THREADS_COMMAND
    {"threads", cmd_threads, 0},
#endif
#if ENABLE_SI5351_REG_WRITE
    {"si", cmd_si5351reg, CMD_WAIT_MUTEX},
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
