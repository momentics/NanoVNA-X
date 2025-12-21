/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Originally written using elements from Dmitry (DiSlord) dislordlive@gmail.com
 * Originally written using elements from TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
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

#include "runtime/runtime_features.h"
#include "runtime/runtime_entry.h"
#include "rf/sweep/sweep_orchestrator.h"

#include "ch.h"
#include "hal.h"
#include "platform/peripherals/si5351.h"
#include "nanovna.h"
#include "rf/engine/measurement_commands.h"
#include "interfaces/cli/shell_service.h"
#include "interfaces/cli/shell_commands.h"
#include "platform/peripherals/usbcfg.h"
#include "platform/hal.h"
#include "infra/storage/config_service.h"
#include "infra/event/event_bus.h"
#include "version_info.h"
#include "rf/engine/measurement_engine.h"
#include "interfaces/ports/processing_port.h"
#include "interfaces/ports/ui_port.h"
#include "interfaces/ports/usb_command_server_port.h"
#include "platform/boards/stm32_peripherals.h"
#include "infra/state/state_manager.h"
#include "ui/display/display_presenter.h"
#include "ui/controller/ui_controller.h"
#include "platform/boards/board_events.h"
#include "ui/core/ui_task.h"

#ifdef __LCD_BRIGHTNESS__
void lcd_set_brightness(uint16_t brightness);
#endif

#include <stdalign.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if defined(NANOVNA_F303)
#define CLI_USAGE_ENABLED 1
#else
#define CLI_USAGE_ENABLED 0
#endif

#if CLI_USAGE_ENABLED
#define CLI_PRINT_USAGE(...) shell_printf(__VA_ARGS__)
#else
#define CLI_PRINT_USAGE(...) do { (void)0; } while (0)
#endif

/*
 *  Shell settings
 */
// If need run shell as thread (use more amount of memory for stack), after
// enable this need reduce spi_buffer size, by default shell runs in main thread
// #define VNA_SHELL_THREAD

static event_bus_t app_event_bus;
static event_bus_subscription_t app_event_slots[6];

#define APP_EVENT_QUEUE_DEPTH 6U
static msg_t app_event_queue_storage[APP_EVENT_QUEUE_DEPTH];
static event_bus_queue_node_t app_event_nodes[APP_EVENT_QUEUE_DEPTH];

static board_events_t board_events;
static const display_presenter_t lcd_display_presenter = {.context = NULL,
                                                          .api = &display_presenter_lcd_api};

static measurement_engine_t measurement_engine;
const processing_port_t processing_port __attribute__((unused)) = {.context = NULL,
                                                                          .api = &processing_port_api};
const ui_module_port_t ui_port __attribute__((unused)) = {.context = NULL,
                                                                 .api = &ui_port_api};
const usb_command_server_port_t usb_port __attribute__((unused)) = {
    .context = NULL, .api = &usb_command_server_port_api};

static bool app_measurement_can_start_sweep(measurement_engine_port_t* port,
                                            measurement_engine_request_t* request);
static void app_measurement_handle_result(measurement_engine_port_t* port,
                                          const measurement_engine_result_t* result);
static void app_measurement_service_loop(measurement_engine_port_t* port);

static measurement_engine_port_t measurement_engine_port;

#define shell_register_commands(table) usb_port.api->register_commands((table))
#define shell_init_connection() usb_port.api->init_connection()
#define shell_check_connect() usb_port.api->check_connect()
#define shell_printf(...) usb_port.api->printf(__VA_ARGS__)
#define shell_stream_write(buffer, size) usb_port.api->stream_write((buffer), (size))
#define shell_update_speed(speed) usb_port.api->update_speed((speed))
#define shell_service_pending_commands() usb_port.api->service_pending_commands()
#define shell_parse_command(line, argc, argv, name_out)                                            \
  usb_port.api->parse_command((line), (argc), (argv), (name_out))
#define shell_request_deferred_execution(command, argc, argv)                                      \
  usb_port.api->request_deferred_execution((command), (argc), (argv))
#define vna_shell_read_line(line, max_size) usb_port.api->read_line((line), (max_size))
#define vna_shell_execute_cmd_line(line) usb_port.api->execute_cmd_line((line))
#define shell_attach_bus(bus) usb_port.api->attach_event_bus((bus))
#define shell_on_session_start(cb) usb_port.api->on_session_start((cb))
#define shell_on_session_stop(cb) usb_port.api->on_session_stop((cb))

static void usb_command_session_started(void);
static void usb_command_session_stopped(void);

#ifdef __USE_SD_CARD__
static FATFS fs_volume_instance;
static FIL fs_file_instance;

static void usb_command_session_started(void) {
  shell_printf(VNA_SHELL_NEWLINE_STR "NanoVNA-X Shell" VNA_SHELL_NEWLINE_STR);
}

static void usb_command_session_stopped(void) {}

FATFS* filesystem_volume(void) {
  return &fs_volume_instance;
}

FIL* filesystem_file(void) {
  return &fs_file_instance;
}

#else
static void usb_command_session_started(void) {
  // Empty when SD card support is disabled
}

static void usb_command_session_stopped(void) {}

// Forward declarations for when SD card is disabled (to match the signature)
void* filesystem_volume(void) {
  return NULL;  // Return NULL when SD card support is disabled
}

void* filesystem_file(void) {
  return NULL;  // Return NULL when SD card support is disabled
}
#endif

// Shell frequency printf format
// #define VNA_FREQ_FMT_STR         "%lu"
#define VNA_FREQ_FMT_STR "%u"

#define VNA_SHELL_FUNCTION(command_name) static void command_name(int argc, char* argv[])

// Shell command line buffer, args, nargs, and function ptr
static char shell_line[VNA_SHELL_MAX_LENGTH];

volatile uint8_t sweep_mode;
static thread_t *sweep_thread = NULL;
bool display_inhibited = false;

// Flag to indicate when calibration is in progress to prevent UI flash operations during critical phases
volatile bool calibration_in_progress = false;

// ... (existing includes and defines)

// New Accessors - Trampolines removed, inline in header or renamed functions

#if defined(NANOVNA_F303)
#define CCM_RAM __attribute__((section(".ccm")))
#else
#define CCM_RAM
#endif

// Sweep measured data - aligned for DMA operations
alignas(8) CCM_RAM float measured[2][SWEEP_POINTS_MAX][2];

// ... (existing variables)


static int16_t battery_last_mv;
static systime_t battery_next_sample = 0;

#ifndef VBAT_MEASURE_INTERVAL
#define BATTERY_REDRAW_INTERVAL S2ST(5)
#else
#define BATTERY_REDRAW_INTERVAL VBAT_MEASURE_INTERVAL
#endif

// Version text, displayed in Config->Version menu, also send by info command
const char* const info_about[] = {
    "Board: " BOARD_NAME, "NanoVNA-X maintainer: @momentics <momentics@gmail.com>",
    "Version: " NANOVNA_VERSION_STRING " ["
    "p:" define_to_STR(
        SWEEP_POINTS_MAX) ", "
                          "IF:" define_to_STR(
                              FREQUENCY_IF_K) "k, "
                                              "ADC:" define_to_STR(
                                                  AUDIO_ADC_FREQ_K1) "k, "
                                                                     "Lcd:" define_to_STR(LCD_WIDTH) "x" define_to_STR(
                                                                         LCD_HEIGHT) "]",
    "Build Time: " __DATE__ " - " __TIME__, 0 // sentinel
};

// Allow draw some debug on LCD
#if DEBUG_CONSOLE_SHOW
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

static void schedule_battery_redraw(void) {
  systime_t now = chVTGetSystemTimeX();
  if ((int32_t)(now - battery_next_sample) < 0) {
    return;
  }
  battery_next_sample = now + BATTERY_REDRAW_INTERVAL;
  int16_t vbat = adc_vbat_read();
  if (vbat == battery_last_mv) {
    return;
  }
  battery_last_mv = vbat;
  request_to_redraw(REDRAW_BATTERY);
}

static bool app_measurement_can_start_sweep(measurement_engine_port_t* port,
                                            measurement_engine_request_t* request) {
  (void)port;
  if (request != NULL) {
    request->break_on_operation = true;
  }
  return (sweep_mode & (SWEEP_ENABLE | SWEEP_ONCE)) != 0;
}

static void app_measurement_handle_result(measurement_engine_port_t* port,
                                          const measurement_engine_result_t* result) {
  (void)port;
  sweep_mode &= (uint8_t)~SWEEP_ONCE;
  if (result == NULL || !result->completed) {
    return;
  }
  if ((props_mode & DOMAIN_MODE) == DOMAIN_TIME) {
    app_measurement_transform_domain(result->sweep_mask);
  }
  request_to_redraw(REDRAW_PLOT);
}

// Measurement Command Mailbox
#define MEASUREMENT_CMD_QUEUE_SIZE 4
static msg_t measurement_cmd_queue[MEASUREMENT_CMD_QUEUE_SIZE];
static MAILBOX_DECL(measurement_cmd_mbox, measurement_cmd_queue, MEASUREMENT_CMD_QUEUE_SIZE);

void measurement_post_command(measurement_command_t cmd) {
    msg_t encoded_cmd = (msg_t)cmd.type;
    
    // Simple encoding: Type in lower 8 bits, data in upper 24 bits
    switch (cmd.type) {
        case CMD_MEASURE_START:
            if (cmd.data.start.oneshot) {
                encoded_cmd |= (1 << 8);
            }
            break;
        case CMD_MEASURE_UPDATE_CONFIG:
            // Assuming flags fit in 16 bits
            // If flags are larger, we might need a different approach, but currently flags are small.
            // (cmd.data.config.flags << 8)
            break;
        default:
            break;
    }

    chMBPost(&measurement_cmd_mbox, encoded_cmd, TIME_IMMEDIATE);
}

static void app_measurement_service_loop(measurement_engine_port_t* port) {
  (void)port;
  shell_service_pending_commands();
  sweep_mode |= SWEEP_UI_MODE;
  // ui_port.api->process(); // Moved to ui_task
  sweep_mode &= (uint8_t)~SWEEP_UI_MODE;
  schedule_battery_redraw();
#if !DEBUG_CONSOLE_SHOW
  // draw_all(); // Moved to ui_task
#endif
  state_manager_service();
}

static THD_WORKING_AREA(waThread1, 800); // 800 bytes sufficient for Sweep Engine (Leaf)
static THD_FUNCTION(Thread1, arg) {
  (void)arg;
  chRegSetThreadName("sweep");
  sweep_thread = chThdGetSelfX();
#ifdef __FLIP_DISPLAY__
  if (VNA_MODE(VNA_MODE_FLIP_DISPLAY))
    lcd_set_flip(true);
#endif
  
  // Initialize command pool - REMOVED (using direct encoding)

  while (1) {
    msg_t msg;
    
    // Check for commands
    // If sweeping is enabled OR engine is busy, we must not block indefinitely
    bool engine_running = ((sweep_mode & SWEEP_ENABLE) != 0) || sweep_is_active();
    systime_t timeout = engine_running ? TIME_IMMEDIATE : TIME_INFINITE;

    if (chMBFetch(&measurement_cmd_mbox, &msg, timeout) == MSG_OK) {
        measurement_command_type_t type = (measurement_command_type_t)(msg & 0xFF);
        
        switch (type) {
            case CMD_MEASURE_START:
                sweep_mode |= SWEEP_ENABLE;
                if ((msg >> 8) & 1) { // Process data
                    sweep_mode |= SWEEP_ONCE;
                } else {
                    sweep_mode &= (uint8_t)~SWEEP_ONCE;
                }
                break;
            case CMD_MEASURE_STOP:
                sweep_mode &= (uint8_t)~SWEEP_ENABLE;
                break;
            case CMD_MEASURE_SINGLE:
                sweep_mode |= SWEEP_ENABLE | SWEEP_ONCE;
                break;
             // Handle config updates or other commands here
            default:
                break;
        }
    }
    
    // Re-evaluate running state after command processing
    engine_running = ((sweep_mode & SWEEP_ENABLE) != 0) || sweep_is_active();

    if (engine_running) {
        measurement_engine_tick(&measurement_engine);
        // Thread1 is now lower priority, so it will be preempted by UI/Shell naturally.
        // No explicit Yield needed.
    }
  }
}

#include "rf/sweep/sweep_orchestrator.h"

void app_measurement_pause(void) {
  if (sweep_thread != NULL && chThdGetSelfX() == sweep_thread) {
    // We are running in the sweep thread (e.g. from Shell Service callback)
    // Directly modify state to avoid self-deadlock on Mailbox or Wait
    sweep_mode &= (uint8_t)~SWEEP_ENABLE;
  } else {
    measurement_command_t cmd;
    cmd.type = CMD_MEASURE_STOP;
    measurement_post_command(cmd);
    // Wait for the sweep thread to actually stop and the engine to become idle
    sweep_wait_for_idle();
  }
}

void app_measurement_enable(void) {
  if (sweep_thread != NULL && chThdGetSelfX() == sweep_thread) {
     sweep_mode |= SWEEP_ENABLE;
     sweep_mode &= (uint8_t)~SWEEP_ONCE;
  } else {
      measurement_command_t cmd;
      cmd.type = CMD_MEASURE_START;
      cmd.data.start.oneshot = false;
      measurement_post_command(cmd);
  }
}

void toggle_sweep(void) {
  if (app_measurement_is_enabled()) {
    app_measurement_pause();
  } else {
    app_measurement_enable();
  }
}

config_t config = {
    .magic = CONFIG_MAGIC,
    ._harmonic_freq_threshold = FREQUENCY_THRESHOLD,
    ._IF_freq = FREQUENCY_OFFSET,
    ._touch_cal = DEFAULT_TOUCH_CONFIG,
    ._vna_mode =
        (1 << VNA_MODE_BACKUP) | (1 << VNA_MODE_USB_UID), // Enable backup + unique USB serial by default
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

alignas(8) properties_t current_props;

int load_properties(uint32_t id) {
  int r = caldata_recall(id);
  app_measurement_update_frequencies();
#ifdef __VNA_MEASURE_MODULE__
  plot_set_measure_mode(current_props._measure);
#endif
  return r;
}


void set_bandwidth(uint16_t bw_count) {
  config._bandwidth = bw_count & 0x1FF;
  request_to_redraw(REDRAW_BACKUP | REDRAW_FREQUENCY);
  update_backup_data();
}

#ifdef __LCD_BRIGHTNESS__
void set_brightness(uint8_t brightness) {
  config._brightness = brightness;
  lcd_set_brightness(brightness);
  update_backup_data();
}
#else
void set_brightness(uint8_t brightness) {
  (void)brightness;
}
#endif

uint32_t get_bandwidth_frequency(uint16_t bw_freq) {
  return (AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT) / (bw_freq + 1);
}

#define MAX_BANDWIDTH (AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT)
#define MIN_BANDWIDTH ((AUDIO_ADC_FREQ / AUDIO_SAMPLES_COUNT) / 512 + 1)

static void update_marker_index(freq_t start, freq_t stop, uint16_t points);

void set_sweep_points(uint16_t points) {
  if (points > SWEEP_POINTS_MAX)
    points = SWEEP_POINTS_MAX;
  if (points < SWEEP_POINTS_MIN)
    points = SWEEP_POINTS_MIN;
  if (points == sweep_points)
    return;
  sweep_points = points;
  app_measurement_update_frequencies();
  update_backup_data();
  state_manager_mark_dirty();

  freq_t start = 0;
  freq_t stop = 0;
  sweep_get_ordered(&start, &stop);
  update_marker_index(start, stop, sweep_points);
  update_grid(start, stop);
  if (need_interpolate(start, stop, sweep_points))
    cal_status |= CALSTAT_INTERPOLATED;
  else
    cal_status &= (uint16_t)~CALSTAT_INTERPOLATED;

  request_to_redraw(REDRAW_BACKUP | REDRAW_PLOT | REDRAW_CAL_STATUS | REDRAW_FREQUENCY |
                    REDRAW_AREA);
  sweep_service_reset_progress();
}

/*
 * Frequency list functions
 */
bool need_interpolate(freq_t start, freq_t stop, uint16_t points) {
  return start != cal_frequency0 || stop != cal_frequency1 || points != cal_sweep_points;
}

#define SCAN_MASK_OUT_FREQ 0b00000001
#define SCAN_MASK_OUT_DATA0 0b00000010
#define SCAN_MASK_OUT_DATA1 0b00000100
#define SCAN_MASK_NO_CALIBRATION 0b00001000
#define SCAN_MASK_NO_EDELAY 0b00010000
#define SCAN_MASK_NO_S21OFFS 0b00100000

void sweep_get_ordered(freq_t* start, freq_t* stop) {
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
#define SCAN_MASK_BINARY 0b10000000

static void update_marker_index(freq_t start, freq_t stop, uint16_t points) {
  uint32_t i;
  freq_t step;
  if (points > 1)
    step = (stop - start) / (points - 1);
  else
    step = 1;
  for (i = 0; i < MARKERS_MAX; i++) {
    if (markers[i].enabled) {
        int32_t index = (int32_t)(markers[i].frequency - start) / step;
        if (index < 0) index = 0;
        if (index >= points) index = points - 1;
        set_marker_index(i, index);
    }
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
}

void set_trace_scale(int t, float scale) {
  if (trace[t].scale == scale) return;
  trace[t].scale = scale;
  request_to_redraw(REDRAW_PLOT | REDRAW_AREA | REDRAW_MARKER);
}

void set_trace_refpos(int t, float refpos) {
  if (trace[t].refpos == refpos) return;
  trace[t].refpos = refpos;
  request_to_redraw(REDRAW_PLOT | REDRAW_AREA | REDRAW_MARKER);
}

void set_trace_type(int t, int type, int channel) {
  if (trace[t].type == type && trace[t].channel == channel) return;
  trace[t].type = type;
  trace[t].channel = channel;
  request_to_redraw(REDRAW_PLOT | REDRAW_AREA | REDRAW_MARKER);
}

void set_trace_enable(int t, bool enable) {
  if (trace[t].enabled == enable) return;
  trace[t].enabled = enable;
  request_to_redraw(REDRAW_PLOT | REDRAW_AREA | REDRAW_MARKER);
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

void set_marker_index(int m, int idx) {
  if (idx < 0) idx = 0;
  if (idx >= sweep_points) idx = sweep_points - 1;
  if (markers[m].enabled) request_to_draw_marker(markers[m].index);
  markers[m].index = idx;
  markers[m].frequency = get_frequency(idx);
  request_to_redraw(REDRAW_MARKER);
}

freq_t get_marker_frequency(int marker) {
  return markers[marker].frequency;
}

void set_sweep_frequency_internal(uint16_t type, freq_t freq, bool enforce_order) {
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
    update_backup_data();
    return;
  }
  app_measurement_update_frequencies();
  update_backup_data();
  state_manager_mark_dirty();
}

void set_sweep_frequency(uint16_t type, freq_t freq) {
  set_sweep_frequency_internal(type, freq, true);
}

void reset_sweep_frequency(void) {
  frequency0 = cal_frequency0;
  frequency1 = cal_frequency1;
  sweep_points = cal_sweep_points;
  app_measurement_update_frequencies();
  update_backup_data();
  state_manager_mark_dirty();

  freq_t start = 0;
  freq_t stop = 0;
  sweep_get_ordered(&start, &stop);
  update_marker_index(start, stop, sweep_points);
  update_grid(start, stop);
  if (need_interpolate(start, stop, sweep_points))
    cal_status |= CALSTAT_INTERPOLATED;
  else
    cal_status &= (uint16_t)~CALSTAT_INTERPOLATED;

  request_to_redraw(REDRAW_BACKUP | REDRAW_PLOT | REDRAW_CAL_STATUS | REDRAW_FREQUENCY |
                    REDRAW_AREA);
  sweep_service_reset_progress();
}

//
static void vna_shell_execute_line(char* line) {
  DEBUG_LOG(0, line); // debug console log
  uint16_t argc = 0;
  char** argv = NULL;
  const char* command_name = NULL;
  const vna_shell_command* cmd = shell_parse_command(line, &argc, &argv, &command_name);
  if (cmd) {
    uint16_t cmd_flag = cmd->flags;
    bool auto_resume = false;
    if ((cmd_flag & CMD_RUN_IN_UI) && (sweep_mode & SWEEP_UI_MODE)) {
      cmd_flag &= (uint16_t)~CMD_WAIT_MUTEX;
    }
    if (cmd_flag & CMD_BREAK_SWEEP) {
      if (sweep_mode & SWEEP_ENABLE) {
        auto_resume = true;
      }
      ui_controller_request_console_break();
      app_measurement_pause();
    }
    if (cmd_flag & CMD_WAIT_MUTEX) {
      if (auto_resume) {
        shell_set_auto_resume(true);
      } else {
        shell_set_auto_resume(false);
      }
      shell_request_deferred_execution(cmd, argc, argv);
    } else {
      cmd->sc_function((int)argc, argv);
      if (auto_resume && !(cmd_flag & CMD_NO_AUTO_RESUME)) {
        app_measurement_enable();
      }
    }
  } else if (command_name && *command_name) {
    shell_printf("%s?" VNA_SHELL_NEWLINE_STR, command_name);
  }
}

#ifdef __SD_CARD_LOAD__
#ifndef __USE_SD_CARD__
#error "Need enable SD card support __USE_SD_CARD__ in nanovna.h, for use __SD_CARD_LOAD__"
#endif
bool sd_card_load_config(void) {
  if (f_mount(filesystem_volume(), "", 1) != FR_OK)
    return FALSE;

  FIL* const config_file = filesystem_file();
  if (f_open(config_file, "config.ini", FA_OPEN_EXISTING | FA_READ) != FR_OK)
    return FALSE;

  char* buf = (char*)spi_buffer;
  UINT size = 0;

  uint16_t j = 0, i;
  bool last_was_cr = false;
  while (f_read(config_file, buf, 512, &size) == FR_OK && size > 0) {
    i = 0;
    while (i < size) {
      uint8_t c = buf[i++];
      if (c == '\r' || c == '\n') {
        if (c == '\n' && last_was_cr) {
          last_was_cr = false;
          continue;
        }
        shell_line[j] = 0;
        if (j > 0) {
          vna_shell_execute_cmd_line(shell_line);
        }
        j = 0;
        last_was_cr = (c == '\r');
        continue;
      }
      last_was_cr = false;
      if (c < 0x20)
        continue;
      if (j < VNA_SHELL_MAX_LENGTH - 1)
        shell_line[j++] = (char)c;
    }
  }
  if (j > 0) {
    shell_line[j] = 0;
    vna_shell_execute_cmd_line(shell_line);
  }
  f_close(config_file);
  return TRUE;
}
#endif

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
int runtime_main(void) {
  /*
   * Initialize ChibiOS systems
   */
  halInit();
  chSysInit();

#if defined(NANOVNA_F303)
  // Zero initialize CCM RAM data (not handled by startup)
  memset(measured, 0, sizeof(measured));
#endif

  sweep_mode = SWEEP_ENABLE;
  battery_last_mv = INT16_MIN;

  /*
   * Watchdog configuration
   * LSI = 40kHz (typical).
   * Prescaler = 32 -> 1.25 kHz.
   * Reload = 3000 -> ~2.4s timeout.
   */
  platform_init();

  const PlatformDrivers* drivers = platform_get_drivers();
  if (drivers != NULL) {
    if (drivers->init) {
      drivers->init();
    }
    // Display initialization moved to end of startup sequence to match NanoVNA-D reference
    // if (drivers->display && drivers->display->init) {
    //   drivers->display->init();
    // }
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

  config_service_init();
  event_bus_init(&app_event_bus, app_event_slots, ARRAY_COUNT(app_event_slots),
                 app_event_queue_storage, ARRAY_COUNT(app_event_queue_storage),
                 app_event_nodes, ARRAY_COUNT(app_event_nodes));
  shell_attach_bus(&app_event_bus);
  shell_on_session_start(usb_command_session_started);
  shell_on_session_stop(usb_command_session_stopped);
  board_events_init(&board_events);

  ui_controller_port_t ui_controller_port = {
      .board_events = &board_events,
      .display = &lcd_display_presenter,
      .config_events = &app_event_bus,
  };
  ui_controller_configure(&ui_controller_port);

  measurement_engine_port.context = NULL;
  measurement_engine_port.can_start_sweep = app_measurement_can_start_sweep;
  measurement_engine_port.handle_result = app_measurement_handle_result;
  measurement_engine_port.service_loop = app_measurement_service_loop;
  measurement_engine_init(&measurement_engine, &measurement_engine_port, &app_event_bus, drivers);
  

  /*
   * restore config and calibration 0 slot from flash memory, also if need use backup data
   */
  state_manager_init();

#ifdef USE_VARIABLE_OFFSET
  si5351_set_frequency_offset(IF_OFFSET);
#endif
  /*
   * Init Shell console commands
   */
  shell_register_commands(commands);
  
  /*
   * Initialize USB Shell Connection
   * Moved here to match NanoVNA-D reference (before LCD init)
   */
  shell_init_connection();

  /*
   * LCD Initialize
   * Init LCD after USB but BEFORE Codec/I2S to match NanoVNA-D reference.
   */
  if (drivers->display && drivers->display->init) {
    drivers->display->init();
  }


  /*
   * tlv320aic Initialize (audio codec)
   */
  tlv320aic3204_init();


  /*
   * Enable codec clocks output FIRST
   * This ensures I2S inputs are driven (not floating) when I2S peripheral is enabled.
   * Prevents F072 hang due to noise on floating I2S pins.
   */
  tlv320aic3204_start_clocks();
  chThdSleepMilliseconds(200); // Short stabilization wait, increased from 10ms

  /*
   * I2S Initialize
   * Now safe to enable I2S as Slave, as clocks are present.
   */
  init_i2s((void*)sweep_service_rx_buffer(),
           (AUDIO_BUFFER_LEN * 2) * sizeof(audio_sample_t) / sizeof(int16_t));

/*
   * SD Card init (if inserted) allow fix issues
   * with partial writes (need read some sectors)
   */
#if HAL_USE_MMC_SPI
  if (drivers->storage && drivers->storage->init) {
    // Only verify SD card if we have filesystem usage?
    // In current HAL storage->init is empty, but we might add verification here
  }
#endif

  /*
   * Initialize USB Shell Connection
   * This ensures core peripherals (Codec, I2S, LCD) are stable before
   * exposing the system to potential USB PHY noise or interrupt floods
   * (especially if cable is disconnected/floating).
   */
  // shell_init_connection(); // Moved earlier

   // drivers->display->init(); // Moved to Phase 2 position (before Codec)
/*
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
  // Start UI Task LAST to ensure all hardware (LCD, Touch, etc) is ready
  ui_task_init(); 
  
  chThdCreateStatic(waThread1, sizeof(waThread1), LOWPRIO, Thread1, NULL);

  while (1) {
    if (!shell_check_connect()) {
      chThdSleepMilliseconds(1000);
      continue;
    }
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
      else {
        chThdSleepMilliseconds(200);
      }
    } while (shell_check_connect());
#endif
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

__attribute__((used)) void hard_fault_handler_c(uint32_t* sp, const hard_fault_extra_registers_t* extra,
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

static const char* const trc_channel_name[] = {"S11", "S21"};

const char* get_trace_chname(int t) {
  return trc_channel_name[trace[t].channel & 1];
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

#ifdef __REMOTE_DESKTOP__
void send_region(remote_region_t* rd, uint8_t* buf, uint16_t size) {
  if (SDU1.config->usbp->state == USB_ACTIVE) {
    shell_stream_write(rd, sizeof(remote_region_t));
    shell_stream_write(buf, size);
    shell_stream_write(VNA_SHELL_PROMPT_STR VNA_SHELL_NEWLINE_STR, 6);
  } else
    app_display_set_inhibited(false);
}
#endif
