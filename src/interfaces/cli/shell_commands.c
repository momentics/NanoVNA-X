#include "interfaces/cli/shell_commands.h"
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

#define FREQ_IS_CENTERSPAN()   (props_mode&TD_CENTER_SPAN)
#define ST_START 0
#define ST_STOP 1
#define ST_CENTER 2
#define ST_SPAN 3
#define ST_CW 4
#define ST_STEP 5
#define ST_VAR 6

#define VNA_SHELL_FUNCTION(command_name) static __attribute__((unused)) void command_name(int argc __attribute__((unused)), char* argv[] __attribute__((unused)))
#define VNA_FREQ_FMT_STR "%u"
#if defined(NANOVNA_F303)
#define CLI_USAGE_ENABLED 1
#else
#define CLI_USAGE_ENABLED 0
#endif

#if CLI_USAGE_ENABLED
#define PRINT_USAGE(...) shell_printf(__VA_ARGS__)
#else
#define PRINT_USAGE(...) do { (void)0; } while (0)
#endif

// Macros from runtime_entry.c
#define CLI_PRINT_USAGE PRINT_USAGE
#define VNA_SHELL_NEWLINE_STR "\r\n"

// Helper for set_power (moved from runtime_entry.c)
void set_power(uint8_t value) {
  request_to_redraw(REDRAW_CAL_STATUS);
  if (value > SI5351_CLK_DRIVE_STRENGTH_8MA)
    value = SI5351_CLK_DRIVE_STRENGTH_AUTO;
  if (current_props._power == value)
    return;
  current_props._power = value;
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
    shell_printf("usage: power {0|1|2|3|auto}" VNA_SHELL_NEWLINE_STR);
    return;
  }
  if (get_str_index(argv[0], "auto") == 0) {
     set_power(SI5351_CLK_DRIVE_STRENGTH_AUTO);
     return;
  }
  uint32_t val = my_atoui(argv[0]);
  set_power(val);
}

VNA_SHELL_FUNCTION(cmd_offset) {
#ifdef USE_VARIABLE_OFFSET
  if (argc != 1) {
    shell_printf("%d" VNA_SHELL_NEWLINE_STR, IF_OFFSET);
    return;
  }
  int32_t offset = my_atoi(argv[0]);
  si5351_set_frequency_offset(offset);
#endif
}

VNA_SHELL_FUNCTION(cmd_time) {
  // Not implemented
  shell_printf("Not implemented" VNA_SHELL_NEWLINE_STR);
}

VNA_SHELL_FUNCTION(cmd_dac) {
#ifdef __VNA_ENABLE_DAC__
  if (argc != 1) return;
  // si5351_dvc_write(my_atoui(argv[0])); // Assuming this function exists or similar
#endif
}

VNA_SHELL_FUNCTION(cmd_measure) {
#ifdef __VNA_MEASURE_MODULE__
    if (argc!=1) return;
    // plot_set_measure_mode(my_atoi(argv[0]));
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
  const freq_t original_start = get_sweep_frequency(ST_START);
  const freq_t original_stop = get_sweep_frequency(ST_STOP);
  const uint16_t original_points = sweep_points;
  bool restore_config = false;

  if (argc < 2 || argc > 4) {
    PRINT_USAGE("usage: scan {start(Hz)} {stop(Hz)} [points] [outmask]" VNA_SHELL_NEWLINE_STR);
    return;
  }

  start = my_atoui(argv[0]);
  stop = my_atoui(argv[1]);
  if (start == 0 || stop == 0 || start > stop || start < 50000 || stop > 900000000U) {
    shell_printf("frequency range is invalid" VNA_SHELL_NEWLINE_STR);
    return;
  }
  if (start != original_start || stop != original_stop)
    restore_config = true;
  if (argc >= 3) {
    points = my_atoui(argv[2]);
    if (points == 0 || points > SWEEP_POINTS_MAX) {
      shell_printf("sweep points exceeds range " define_to_STR(SWEEP_POINTS_MAX) VNA_SHELL_NEWLINE_STR);
      return;
    }
    set_sweep_points(points);
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

  set_sweep_points(points);
  app_measurement_set_frequencies(start, stop, points);
  if (sweep_ch & (SWEEP_CH0_MEASURE | SWEEP_CH1_MEASURE))
    app_measurement_sweep(false, sweep_ch);
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
    set_sweep_points(original_points);
    app_measurement_update_frequencies();
  }
  resume_sweep();
}

VNA_SHELL_FUNCTION(cmd_scan_bin) {
#if ENABLE_SCANBIN_COMMAND
  sweep_mode |= SWEEP_BINARY;
  cmd_scan(argc, argv);
#endif
}

VNA_SHELL_FUNCTION(cmd_bandwidth) {
  if (argc == 0) {
    shell_printf("bandwidth %d (%uHz)" VNA_SHELL_NEWLINE_STR, config._bandwidth,
                 get_bandwidth_frequency(config._bandwidth));
    return;
  }
  uint32_t user_bw = my_atoui(argv[0]);
  if (user_bw >= 30) {
#ifdef BANDWIDTH_8000
    if (user_bw >= 8000) user_bw = BANDWIDTH_8000; else
#endif
#ifdef BANDWIDTH_4000
    if (user_bw >= 4000) user_bw = BANDWIDTH_4000; else
#endif
#ifdef BANDWIDTH_2000
    if (user_bw >= 2000) user_bw = BANDWIDTH_2000; else
#endif
#ifdef BANDWIDTH_1000
    if (user_bw >= 1000) user_bw = BANDWIDTH_1000; else
#endif
#ifdef BANDWIDTH_333
    if (user_bw >= 333) user_bw = BANDWIDTH_333; else
#endif
#ifdef BANDWIDTH_100
    if (user_bw >= 100) user_bw = BANDWIDTH_100; else
#endif
#ifdef BANDWIDTH_30
    if (user_bw >= 30) user_bw = BANDWIDTH_30; else
#endif
#ifdef BANDWIDTH_10
    user_bw = BANDWIDTH_10;
#else
#ifdef BANDWIDTH_30
    user_bw = BANDWIDTH_30;
#else
    user_bw = 0; // Should not happen given nanovna.h
#endif
#endif
  }
  set_bandwidth(user_bw);
}

VNA_SHELL_FUNCTION(cmd_freq) {
  if (argc > 1) {
    PRINT_USAGE("usage: freq [freq(Hz)]" VNA_SHELL_NEWLINE_STR);
    return; 
  }
  if (argc == 1) {
    freq_t freq = my_atoui(argv[0]);
    if (freq < 50000 || freq > 900000000U) {
       shell_printf("Start frequency %lu out of range" VNA_SHELL_NEWLINE_STR, freq);
       return;
    }
    // set_sweep_frequency_internal(ST_CW, freq, false);
    pause_sweep();
    app_measurement_set_frequency(freq);
    return;
  }
  shell_printf(VNA_FREQ_FMT_STR VNA_SHELL_NEWLINE_STR, get_sweep_frequency(ST_CW));
}

VNA_SHELL_FUNCTION(cmd_sweep) {
  if (argc == 0) {
    shell_printf(VNA_FREQ_FMT_STR " " VNA_FREQ_FMT_STR " %d" VNA_SHELL_NEWLINE_STR,
                 get_sweep_frequency(ST_START), get_sweep_frequency(ST_STOP), sweep_points);
    return;
  } else if (argc > 3) {
    PRINT_USAGE("usage: sweep {start(Hz)} [stop(Hz)] [points]" VNA_SHELL_NEWLINE_STR);
    return;
  }
  // Simplified logic from shell_commands.c
  freq_t value0 = 0;
  freq_t value1 = 0;
  uint32_t value2 = 0;
  if (argc >= 1) value0 = my_atoui(argv[0]);
  if (argc >= 2) value1 = my_atoui(argv[1]);
  if (argc >= 3) value2 = my_atoui(argv[2]);
  
  if (value0) set_sweep_frequency_internal(ST_START, value0, false);
  if (value1) set_sweep_frequency_internal(ST_STOP, value1, false);
  if (value2) set_sweep_points(value2);
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
        shell_printf("%f %f" VNA_SHELL_NEWLINE_STR, snapshot.data[i][0], snapshot.data[i][1]);
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
    shell_printf("%f %f" VNA_SHELL_NEWLINE_STR, array[i][0], array[i][1]);
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
               "Do reset manually." VNA_SHELL_NEWLINE_STR);
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

  if (argc == 0) {
      for (int t = 0; t < MARKERS_MAX; t++)
          if (markers[t].enabled)
              shell_printf("%d %d " VNA_FREQ_FMT_STR VNA_SHELL_NEWLINE_STR, t + 1, markers[t].index, markers[t].frequency);
      return;
  }
  // Simplified marker logic from runtime_entry.c
  int t = my_atoi(argv[0]) - 1;
  if (t < 0 || t >= MARKERS_MAX) return;
  if (argc > 1) {
      // Toggle or set index
      if (get_str_index(argv[1], "off") == 0) markers[t].enabled = FALSE;
      else {
          markers[t].enabled = TRUE;
          set_marker_index(t, my_atoi(argv[1]));
      }
  } else {
      markers[t].enabled = TRUE;
      active_marker = t;
  }
  request_to_redraw(REDRAW_MARKER | REDRAW_AREA);
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
  alignas(4) audio_sample_t dump[96 * 2];
  int selection = 0;
  if (argc == 1) selection = (my_atoui(argv[0]) == 1) ? 0 : 1;
  sweep_service_prepare_dump(dump, ARRAY_COUNT(dump), selection);
  // tlv320aic3204_select(0);
  sweep_service_start_capture(DELAY_SWEEP_START);
  while (!sweep_service_dump_ready()) __WFI();
  for (i = 0, j = 0; i < (int)ARRAY_COUNT(dump); i++) {
    shell_printf("%6d ", dump[i]);
    if (++j == 12) { shell_printf(VNA_SHELL_NEWLINE_STR); j = 0; }
  }
#endif
}

// Stubs/Empty for others to allow linking if referenced
VNA_SHELL_FUNCTION(cmd_sd_list) {}
VNA_SHELL_FUNCTION(cmd_sd_read) {}
VNA_SHELL_FUNCTION(cmd_sd_delete) {}
VNA_SHELL_FUNCTION(cmd_port) {}
VNA_SHELL_FUNCTION(cmd_stat) {}
VNA_SHELL_FUNCTION(cmd_gain) {}
VNA_SHELL_FUNCTION(cmd_test) {}
VNA_SHELL_FUNCTION(cmd_pause) { pause_sweep(); }
VNA_SHELL_FUNCTION(cmd_resume) { resume_sweep(); }
VNA_SHELL_FUNCTION(cmd_msg) {}
VNA_SHELL_FUNCTION(cmd_refresh) {}
VNA_SHELL_FUNCTION(cmd_touch) {}
VNA_SHELL_FUNCTION(cmd_release) {}
VNA_SHELL_FUNCTION(cmd_vbat) { shell_printf("%d m" S_VOLT VNA_SHELL_NEWLINE_STR, adc_vbat_read()); }
VNA_SHELL_FUNCTION(cmd_tcxo) {}
VNA_SHELL_FUNCTION(cmd_reset) { NVIC_SystemReset(); }
VNA_SHELL_FUNCTION(cmd_smooth) {}
VNA_SHELL_FUNCTION(cmd_config) {}
VNA_SHELL_FUNCTION(cmd_usart_cfg) {}
VNA_SHELL_FUNCTION(cmd_usart) {}
VNA_SHELL_FUNCTION(cmd_vbat_offset) {}
VNA_SHELL_FUNCTION(cmd_help) {}
VNA_SHELL_FUNCTION(cmd_info) {}
VNA_SHELL_FUNCTION(cmd_version) { shell_printf("%s" VNA_SHELL_NEWLINE_STR, NANOVNA_VERSION_STRING); }
VNA_SHELL_FUNCTION(cmd_color) {}
VNA_SHELL_FUNCTION(cmd_i2c) {}
VNA_SHELL_FUNCTION(cmd_si5351reg) {}
VNA_SHELL_FUNCTION(cmd_lcd) {}
VNA_SHELL_FUNCTION(cmd_threads) {}
VNA_SHELL_FUNCTION(cmd_si5351time) {}
VNA_SHELL_FUNCTION(cmd_i2ctime) {}
VNA_SHELL_FUNCTION(cmd_band) {}

const VNAShellCommand commands[] = {
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
#ifdef ENABLE_SD_CARD_COMMAND
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
#ifdef ENABLE_TEST_COMMAND
    {"test", cmd_test, 0},
#endif
    {"touchcal", cmd_touchcal, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP},
    {"touchtest", cmd_touchtest, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP},
    {"pause", cmd_pause, CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
    {"resume", cmd_resume, CMD_WAIT_MUTEX | CMD_BREAK_SWEEP | CMD_RUN_IN_UI | CMD_RUN_IN_LOAD},
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
#ifdef ENABLE_VBAT_OFFSET_COMMAND
    {"vbat_offset", cmd_vbat_offset, CMD_RUN_IN_LOAD},
#endif
#ifdef ENABLE_TRANSFORM_COMMAND
    {"transform", cmd_transform, CMD_RUN_IN_LOAD},
#endif
    {"threshold", cmd_threshold, CMD_RUN_IN_LOAD},
    {"help", cmd_help, 0},
#ifdef ENABLE_INFO_COMMAND
    {"info", cmd_info, 0},
#endif
    {"version", cmd_version, 0},
#ifdef ENABLE_COLOR_COMMAND
    {"color", cmd_color, CMD_RUN_IN_LOAD},
#endif
#ifdef ENABLE_I2C_COMMAND
    {"i2c", cmd_i2c, CMD_WAIT_MUTEX},
#endif
#ifdef ENABLE_SI5351_REG_WRITE
    {"si", cmd_si5351reg, CMD_WAIT_MUTEX},
#endif
#ifdef ENABLE_LCD_COMMAND
    {"lcd", cmd_lcd, CMD_WAIT_MUTEX},
#endif
#if ENABLE_THREADS_COMMAND
    {"threads", cmd_threads, 0},
#endif
#ifdef ENABLE_SI5351_TIMINGS
    {"t", cmd_si5351time, CMD_WAIT_MUTEX},
#endif
#ifdef ENABLE_I2C_TIMINGS
    {"i", cmd_i2ctime, CMD_WAIT_MUTEX},
#endif
#ifdef ENABLE_BAND_COMMAND
    {"b", cmd_band, CMD_WAIT_MUTEX},
#endif
    {NULL, NULL, 0}};

