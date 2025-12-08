/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
 * All rights reserved.
 */
#include "nanovna.h"
#include "ui/ui_internal.h"
#include "ui/input/ui_keypad.h"
#include "ui/display/display_presenter.h"
#include "ui/input/hardware_input.h"
#include "ui/input/ui_touch.h"
#include <string.h>
#include "platform/peripherals/si5351.h"
#include "infra/storage/config_service.h"

// Max keyboard input length
#define NUMINPUT_LEN 12
#define TXTINPUT_LEN (8)

#if NUMINPUT_LEN + 2 > TXTINPUT_LEN + 1
static char kp_buf[NUMINPUT_LEN +
                   2]; // !!!!!! WARNING size must be + 2 from NUMINPUT_LEN or TXTINPUT_LEN + 1
#else
static char kp_buf[TXTINPUT_LEN +
                   1]; // !!!!!! WARNING size must be + 2 from NUMINPUT_LEN or TXTINPUT_LEN + 1
#endif

// We use shared selection variable
// extern int8_t selection; (from ui_internal.h)

enum { NUM_KEYBOARD, TXT_KEYBOARD };

// Keyboard size and position data
static const keypad_pos_t key_pos[] = {
    [NUM_KEYBOARD] = {KP_X_OFFSET, KP_Y_OFFSET, KP_WIDTH, KP_HEIGHT},
    [TXT_KEYBOARD] = {KPF_X_OFFSET, KPF_Y_OFFSET, KPF_WIDTH, KPF_HEIGHT}};

static const keypads_t* keypads;
uint8_t keypad_mode;

static const keypads_t keypads_freq[] = {
    {16, NUM_KEYBOARD},               // 14 + 2 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6
    {0x12, KP_2},                     // 1 2 3 G
    {0x22, KP_3},                     // 0 . < M
    {0x01, KP_4},       {0x11, KP_5},     {0x21, KP_6},     {0x00, KP_7},
    {0x10, KP_8},       {0x20, KP_9},     {0x30, KP_G},     {0x31, KP_M},
    {0x32, KP_k},       {0x33, KP_ENTER}, {0x23, KP_BS},
};

static const keypads_t keypads_ufloat[] = {
    {16, NUM_KEYBOARD},               // 14 + 2 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6
    {0x12, KP_2},                     // 1 2 3
    {0x22, KP_3},                     // 0 . < x
    {0x01, KP_4},       {0x11, KP_5},     {0x21, KP_6},     {0x00, KP_7},
    {0x10, KP_8},       {0x20, KP_9},     {0x33, KP_ENTER}, {0x23, KP_BS},
    {0x30, KP_EMPTY},   {0x31, KP_EMPTY}, {0x32, KP_EMPTY},
};

static const keypads_t keypads_percent[] = {
    {16, NUM_KEYBOARD},               // 14 + 2 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6
    {0x12, KP_2},                     // 1 2 3
    {0x22, KP_3},                     // 0 . < %
    {0x01, KP_4},       {0x11, KP_5},     {0x21, KP_6},       {0x00, KP_7},
    {0x10, KP_8},       {0x20, KP_9},     {0x33, KP_PERCENT}, {0x23, KP_BS},
    {0x30, KP_EMPTY},   {0x31, KP_EMPTY}, {0x32, KP_EMPTY},
};

static const keypads_t keypads_float[] = {
    {16, NUM_KEYBOARD},               // 14 + 2 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6
    {0x12, KP_2},                     // 1 2 3 -
    {0x22, KP_3},                     // 0 . < x
    {0x01, KP_4},       {0x11, KP_5},     {0x21, KP_6},     {0x00, KP_7},
    {0x10, KP_8},       {0x20, KP_9},     {0x32, KP_MINUS}, {0x33, KP_ENTER},
    {0x23, KP_BS},      {0x30, KP_EMPTY}, {0x31, KP_EMPTY},
};

static const keypads_t keypads_mfloat[] = {{16, NUM_KEYBOARD}, // 16 buttons NUM keyboard (4x4 size)
                                           {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9 u
                                           {0x02, KP_1},                     // 4 5 6 m
                                           {0x12, KP_2},                     // 1 2 3 -
                                           {0x22, KP_3},                     // 0 . < x
                                           {0x01, KP_4},       {0x11, KP_5}, {0x21, KP_6},
                                           {0x00, KP_7},       {0x10, KP_8}, {0x20, KP_9},
                                           {0x30, KP_u},       {0x31, KP_m}, {0x32, KP_MINUS},
                                           {0x33, KP_ENTER},   {0x23, KP_BS}};

static const keypads_t keypads_mkufloat[] = {
    {16, NUM_KEYBOARD},               // 15 + 1 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6 m
    {0x12, KP_2},                     // 1 2 3 k
    {0x22, KP_3},                     // 0 . < x
    {0x01, KP_4},       {0x11, KP_5}, {0x21, KP_6},  {0x00, KP_7},  {0x10, KP_8},     {0x20, KP_9},
    {0x31, KP_m},       {0x32, KP_k}, {0x33, KP_X1}, {0x23, KP_BS}, {0x30, KP_EMPTY},
};

static const keypads_t keypads_nfloat[] = {
    {16, NUM_KEYBOARD},               // 16 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9 u
    {0x02, KP_1},                     // 4 5 6 n
    {0x12, KP_2},                     // 1 2 3 p
    {0x22, KP_3},                     // 0 . < -
    {0x01, KP_4},       {0x11, KP_5}, {0x21, KP_6}, {0x00, KP_7},     {0x10, KP_8}, {0x20, KP_9},
    {0x30, KP_u},       {0x31, KP_n}, {0x32, KP_p}, {0x33, KP_MINUS}, {0x23, KP_BS}};

#if 0
//  ABCD keyboard
static const keypads_t keypads_text[] = {
  {40, TXT_KEYBOARD },   // 40 buttons TXT keyboard (10x4 size)
  {0x00, '0'}, {0x10, '1'}, {0x20, '2'}, {0x30, '3'}, {0x40, '4'}, {0x50, '5'}, {0x60, '6'}, {0x70, '7'}, {0x80, '8'}, {0x90, '9'},
  {0x01, 'A'}, {0x11, 'B'}, {0x21, 'C'}, {0x31, 'D'}, {0x41, 'E'}, {0x51, 'F'}, {0x61, 'G'}, {0x71, 'H'}, {0x81, 'I'}, {0x91, 'J'},
  {0x02, 'K'}, {0x12, 'L'}, {0x22, 'M'}, {0x32, 'N'}, {0x42, 'O'}, {0x52, 'P'}, {0x62, 'Q'}, {0x72, 'R'}, {0x82, 'S'}, {0x92, 'T'},
  {0x03, 'U'}, {0x13, 'V'}, {0x23, 'W'}, {0x33, 'X'}, {0x43, 'Y'}, {0x53, 'Z'}, {0x63, '_'}, {0x73, '-'}, {0x83, S_LARROW[0]}, {0x93, S_ENTER[0]},
};
#else
// QWERTY keyboard
static const keypads_t keypads_text[] = {
    {40, TXT_KEYBOARD}, // 40 buttons TXT keyboard (10x4 size)
    {0x00, '1'},        {0x10, '2'}, {0x20, '3'}, {0x30, '4'},         {0x40, '5'},
    {0x50, '6'},        {0x60, '7'}, {0x70, '8'}, {0x80, '9'},         {0x90, '0'},
    {0x01, 'Q'},        {0x11, 'W'}, {0x21, 'E'}, {0x31, 'R'},         {0x41, 'T'},
    {0x51, 'Y'},        {0x61, 'U'}, {0x71, 'I'}, {0x81, 'O'},         {0x91, 'P'},
    {0x02, 'A'},        {0x12, 'S'}, {0x22, 'D'}, {0x32, 'F'},         {0x42, 'G'},
    {0x52, 'H'},        {0x62, 'J'}, {0x72, 'K'}, {0x82, 'L'},         {0x92, '_'},
    {0x03, '-'},        {0x13, 'Z'}, {0x23, 'X'}, {0x33, 'C'},         {0x43, 'V'},
    {0x53, 'B'},        {0x63, 'N'}, {0x73, 'M'}, {0x83, S_LARROW[0]}, {0x93, S_ENTER[0]},
};
#endif

enum {
  KEYPAD_FREQ,
  KEYPAD_UFLOAT,
  KEYPAD_PERCENT,
  KEYPAD_FLOAT,
  KEYPAD_MFLOAT,
  KEYPAD_MKUFLOAT,
  KEYPAD_NFLOAT,
  KEYPAD_TEXT
};
static const keypads_t* keypad_type_list[] = {
    [KEYPAD_FREQ] = keypads_freq,         // frequency input
    [KEYPAD_UFLOAT] = keypads_ufloat,     // unsigned float input
    [KEYPAD_PERCENT] = keypads_percent,   // unsigned float input in percent
    [KEYPAD_FLOAT] = keypads_float,       //   signed float input
    [KEYPAD_MFLOAT] = keypads_mfloat,     //   signed milli/micro float input
    [KEYPAD_MKUFLOAT] = keypads_mkufloat, // unsigned milli/kilo float input
    [KEYPAD_NFLOAT] = keypads_nfloat,     //   signed micro/nano/pico float input
    [KEYPAD_TEXT] = keypads_text          // text input
};

// Get value from keyboard functions
float keyboard_get_float(void) {
  return my_atof(kp_buf);
}
freq_t keyboard_get_freq(void) {
  return my_atoui(kp_buf);
}
uint32_t keyboard_get_uint(void) {
  return my_atoui(kp_buf);
}
int32_t keyboard_get_int(void) {
  return my_atoi(kp_buf);
}

// Keyboard call back functions, allow get value for Keyboard menu button (see menu_keyboard_acb)
// and apply on finish input
UI_KEYBOARD_CALLBACK(input_freq) {
  if (b) {
    if (data == ST_VAR && var_freq)
      plot_printf(b->label, sizeof(b->label), "JOG STEP\n " R_LINK_COLOR "%.3q" S_Hz, var_freq);
    if (data == ST_STEP)
      b->p1.f = (float)get_sweep_frequency(ST_SPAN) / (sweep_points - 1);
    return;
  }
  set_sweep_frequency(data, keyboard_get_freq());
}

UI_KEYBOARD_CALLBACK(input_var_delay) {
  (void)data;
  if (b) {
    if (current_props._var_delay)
      plot_printf(b->label, sizeof(b->label), "JOG STEP\n " R_LINK_COLOR "%F" S_SECOND,
                  current_props._var_delay);
    return;
  }
  current_props._var_delay = keyboard_get_float();
}

// Call back functions for MT_CALLBACK type
UI_KEYBOARD_CALLBACK(input_points) {
  (void)data;
  if (b) {
    b->p1.u = sweep_points;
    return;
  }
  set_sweep_points(keyboard_get_uint());
}

UI_KEYBOARD_CALLBACK(input_amplitude) {
  int type = trace[current_trace].type;
  float scale = get_trace_scale(current_trace);
  float ref = get_trace_refpos(current_trace);
  float bot = (0 - ref) * scale;
  float top = (NGRIDY - ref) * scale;

  if (b) {
    float val = data == 0 ? top : bot;
    if (type == TRC_SWR)
      val += 1.0f;
    plot_printf(b->label, sizeof(b->label), "%s\n " R_LINK_COLOR "%.4F%s",
                data == 0 ? "TOP" : "BOTTOM", val, trace_info_list[type].symbol);
    return;
  }
  float value = keyboard_get_float();
  if (type == TRC_SWR)
    value -= 1.0f; // Hack for SWR trace!
  if (data == 0)
    top = value; // top value input
  else
    bot = value; // bottom value input
  scale = (top - bot) / NGRIDY;
  ref = (top == bot) ? -value : -bot / scale;
  set_trace_scale(current_trace, scale);
  set_trace_refpos(current_trace, ref);
}

UI_KEYBOARD_CALLBACK(input_scale) {
  (void)data;
  if (b)
    return;
  set_trace_scale(current_trace, keyboard_get_float());
}

UI_KEYBOARD_CALLBACK(input_ref) {
  (void)data;
  if (b)
    return;
  set_trace_refpos(current_trace, keyboard_get_float());
}

UI_KEYBOARD_CALLBACK(input_edelay) {
  (void)data;
  if (current_trace == TRACE_INVALID)
    return;
  int ch = trace[current_trace].channel;
  if (b) {
    plot_printf(b->label, sizeof(b->label), "E-DELAY S%d1\n " R_LINK_COLOR "%.7F" S_SECOND, ch + 1,
                current_props._electrical_delay[ch]);
    return;
  }
  set_electrical_delay(ch, keyboard_get_float());
}

UI_KEYBOARD_CALLBACK(input_s21_offset) {
  (void)data;
  if (b) {
    b->p1.f = s21_offset;
    return;
  }
  set_s21_offset(keyboard_get_float());
}

UI_KEYBOARD_CALLBACK(input_velocity) {
  (void)data;
  if (b) {
    b->p1.u = velocity_factor;
    return;
  }
  velocity_factor = keyboard_get_uint();
}

#ifdef __S11_CABLE_MEASURE__
extern float real_cable_len;
UI_KEYBOARD_CALLBACK(input_cable_len) {
  (void)data;
  if (b) {
    if (real_cable_len == 0.0f)
      return;
    plot_printf(b->label, sizeof(b->label), "%s\n " R_LINK_COLOR "%.4F%s", "CABLE LENGTH",
                real_cable_len, S_METRE);
    return;
  }
  real_cable_len = keyboard_get_float();
}
#endif

UI_KEYBOARD_CALLBACK(input_xtal) {
  (void)data;
  if (b) {
    b->p1.u = config._xtal_freq;
    return;
  }
  si5351_set_tcxo(keyboard_get_uint());
}

UI_KEYBOARD_CALLBACK(input_harmonic) {
  (void)data;
  if (b) {
    b->p1.u = config._harmonic_freq_threshold;
    return;
  }
  uint32_t value = keyboard_get_uint();
  config._harmonic_freq_threshold = clamp_harmonic_threshold(value);
  config_service_notify_configuration_changed();
}

UI_KEYBOARD_CALLBACK(input_vbat) {
  (void)data;
  if (b) {
    b->p1.u = config._vbat_offset;
    return;
  }
  config._vbat_offset = keyboard_get_uint();
  config_service_notify_configuration_changed();
}

#ifdef __S21_MEASURE__
UI_KEYBOARD_CALLBACK(input_measure_r) {
  (void)data;
  if (b) {
    b->p1.f = config._measure_r;
    return;
  }
  config._measure_r = keyboard_get_float();
  config_service_notify_configuration_changed();
}
#endif

#ifdef __VNA_Z_RENORMALIZATION__
UI_KEYBOARD_CALLBACK(input_portz) {
  if (b) {
    b->p1.f = data ? current_props._cal_load_r : current_props._portz;
    return;
  }
  if (data)
    current_props._cal_load_r = keyboard_get_float();
  else
    current_props._portz = keyboard_get_float();
}
#endif

#ifdef __USE_RTC__
UI_KEYBOARD_CALLBACK(input_date_time) {
  if (b)
    return;
  int i = 0;
  uint32_t dt_buf[2];
  dt_buf[0] = rtc_get_tr_bcd(); // TR should be read first for sync
  dt_buf[1] = rtc_get_dr_bcd(); // DR should be read second
  //            0    1   2       4      5     6
  // time[] ={sec, min, hr, 0, day, month, year, 0}
  uint8_t* time = (uint8_t*)dt_buf;
  for (; i < 6 && kp_buf[i] != 0; i++)
    kp_buf[i] -= '0';
  for (; i < 6; i++)
    kp_buf[i] = 0;
  for (i = 0; i < 3; i++)
    kp_buf[i] = (kp_buf[2 * i] << 4) | kp_buf[2 * i + 1]; // BCD format
  if (data == KM_RTC_DATE) {
    // Month limit 1 - 12 (in BCD)
    if (kp_buf[1] < 1)
      kp_buf[1] = 1;
    else if (kp_buf[1] > 0x12)
      kp_buf[1] = 0x12;
    // Day limit (depend from month):
    uint8_t day_max = 28 + ((0b11101100000000000010111110111011001100 >> (kp_buf[1] << 1)) & 3);
    day_max = ((day_max / 10) << 4) | (day_max % 10); // to BCD
    if (kp_buf[2] < 1)
      kp_buf[2] = 1;
    else if (kp_buf[2] > day_max)
      kp_buf[2] = day_max;
    time[6] = kp_buf[0]; // year
    time[5] = kp_buf[1]; // month
    time[4] = kp_buf[2]; // day
  } else {
    // Hour limit 0 - 23, min limit 0 - 59, sec limit 0 - 59 (in BCD)
    if (kp_buf[0] > 0x23)
      kp_buf[0] = 0x23;
    if (kp_buf[1] > 0x59)
      kp_buf[1] = 0x59;
    if (kp_buf[2] > 0x59)
      kp_buf[2] = 0x59;
    time[2] = kp_buf[0]; // hour
    time[1] = kp_buf[1]; // min
    time[0] = kp_buf[2]; // sec
  }
  rtc_set_time(dt_buf[1], dt_buf[0]);
}

UI_KEYBOARD_CALLBACK(input_rtc_cal) {
  (void)data;
  if (b) {
    b->p1.f = rtc_get_cal();
    return;
  }
  rtc_set_cal(keyboard_get_float());
}
#endif

#ifdef __USE_SD_CARD__
UI_KEYBOARD_CALLBACK(input_filename) {
  if (b)
    return;
  ui_save_file(kp_buf, data);
}
#endif

const keypads_list keypads_mode_tbl[KM_NONE] = {
    //                      key format     data for cb    text at bottom        callback function
    [KM_START] = {KEYPAD_FREQ, ST_START, "START", input_freq},          // start
    [KM_STOP] = {KEYPAD_FREQ, ST_STOP, "STOP", input_freq},             // stop
    [KM_CENTER] = {KEYPAD_FREQ, ST_CENTER, "CENTER", input_freq},       // center
    [KM_SPAN] = {KEYPAD_FREQ, ST_SPAN, "SPAN", input_freq},             // span
    [KM_CW] = {KEYPAD_FREQ, ST_CW, "CW FREQ", input_freq},              // cw freq
    [KM_STEP] = {KEYPAD_FREQ, ST_STEP, "FREQ STEP", input_freq},        // freq as point step
    [KM_VAR] = {KEYPAD_FREQ, ST_VAR, "JOG STEP", input_freq},           // VAR freq step
    [KM_POINTS] = {KEYPAD_UFLOAT, 0, "POINTS", input_points},           // Points num
    [KM_TOP] = {KEYPAD_MFLOAT, 0, "TOP", input_amplitude},              // top graph value
    [KM_nTOP] = {KEYPAD_NFLOAT, 0, "TOP", input_amplitude},             // top graph value
    [KM_BOTTOM] = {KEYPAD_MFLOAT, 1, "BOTTOM", input_amplitude},        // bottom graph value
    [KM_nBOTTOM] = {KEYPAD_NFLOAT, 1, "BOTTOM", input_amplitude},       // bottom graph value
    [KM_SCALE] = {KEYPAD_UFLOAT, KM_SCALE, "SCALE", input_scale},       // scale
    [KM_nSCALE] = {KEYPAD_NFLOAT, KM_nSCALE, "SCALE", input_scale},     // nano / pico scale value
    [KM_REFPOS] = {KEYPAD_FLOAT, 0, "REFPOS", input_ref},               // refpos
    [KM_EDELAY] = {KEYPAD_NFLOAT, 0, "E-DELAY", input_edelay},          // electrical delay
    [KM_VAR_DELAY] = {KEYPAD_NFLOAT, 0, "JOG STEP", input_var_delay},   // VAR electrical delay
    [KM_S21OFFSET] = {KEYPAD_FLOAT, 0, "S21 OFFSET", input_s21_offset}, // S21 level offset
    [KM_VELOCITY_FACTOR] = {KEYPAD_PERCENT, 0, "VELOCITY%%", input_velocity}, // velocity factor
#ifdef __S11_CABLE_MEASURE__
    [KM_ACTUAL_CABLE_LEN] = {KEYPAD_MKUFLOAT, 0, "CABLE LENGTH",
                             input_cable_len}, // real cable length input for VF calculation
#endif
    [KM_XTAL] = {KEYPAD_FREQ, 0, "TCXO 26M" S_Hz, input_xtal},      // XTAL frequency
    [KM_THRESHOLD] = {KEYPAD_FREQ, 0, "THRESHOLD", input_harmonic}, // Harmonic threshold frequency
    [KM_VBAT] = {KEYPAD_UFLOAT, 0, "BAT OFFSET", input_vbat},       // Vbat offset input in mV
#ifdef __S21_MEASURE__
    [KM_MEASURE_R] = {KEYPAD_UFLOAT, 0, "MEASURE Rl", input_measure_r}, // CH0 port impedance in Om
#endif
#ifdef __VNA_Z_RENORMALIZATION__
    [KM_Z_PORT] = {KEYPAD_UFLOAT, 0, "PORT Z 50" S_RARROW,
                   input_portz}, // Port Z renormalization impedance
    [KM_CAL_LOAD_R] = {KEYPAD_UFLOAT, 1, "STANDARD\n LOAD R",
                       input_portz}, // Calibration standard load R
#endif
#ifdef __USE_RTC__
    [KM_RTC_DATE] = {KEYPAD_UFLOAT, KM_RTC_DATE, "SET DATE\nYY MM DD", input_date_time}, // Date
    [KM_RTC_TIME] = {KEYPAD_UFLOAT, KM_RTC_TIME, "SET TIME\nHH MM SS", input_date_time}, // Time
    [KM_RTC_CAL] = {KEYPAD_FLOAT, 0, "RTC CAL", input_rtc_cal}, // RTC calibration in ppm
#endif
#ifdef __USE_SD_CARD__
    [KM_S1P_NAME] = {KEYPAD_TEXT, FMT_S1P_FILE, "S1P", input_filename},
    [KM_S2P_NAME] = {KEYPAD_TEXT, FMT_S2P_FILE, "S2P", input_filename},
    [KM_BMP_NAME] = {KEYPAD_TEXT, FMT_BMP_FILE, "BMP", input_filename},
#ifdef __SD_CARD_DUMP_TIFF__
    [KM_TIF_NAME] = {KEYPAD_TEXT, FMT_TIF_FILE, "TIF", input_filename},
#endif
    [KM_CAL_NAME] = {KEYPAD_TEXT, FMT_CAL_FILE, "CAL", input_filename},
#ifdef __SD_CARD_DUMP_FIRMWARE__
    [KM_BIN_NAME] = {KEYPAD_TEXT, FMT_BIN_FILE, "BIN", input_filename},
#endif
#endif
};

// Keyboard callback function for UI button
void ui_keyboard_cb(uint16_t data, button_t* b) {
  const keyboard_cb_t cb = keypads_mode_tbl[data].cb;
  if (cb)
    cb(keypads_mode_tbl[data].data, b);
}

static void keypad_draw_button(int id) {
  if (id < 0)
    return;
  button_t button;
  button.fg = LCD_MENU_TEXT_COLOR;

  if (id == selection) {
    button.bg = LCD_MENU_ACTIVE_COLOR;
    button.border = KEYBOARD_BUTTON_BORDER | BUTTON_BORDER_FALLING;
  } else {
    button.bg = LCD_MENU_COLOR;
    button.border = KEYBOARD_BUTTON_BORDER | BUTTON_BORDER_RISE;
  }

  const keypad_pos_t* p = &key_pos[keypads->type];
  int x = p->x_offs + (keypads->buttons[id].pos >> 4) * p->width;
  int y = p->y_offs + (keypads->buttons[id].pos & 0xF) * p->height;
  ui_draw_button(x, y, p->width, p->height, &button);
  uint8_t ch = keypads->buttons[id].c;
  if (ch == KP_EMPTY)
    return;
  if (keypads->type == NUM_KEYBOARD) {
    lcd_drawfont(ch, x + (KP_WIDTH - NUM_FONT_GET_WIDTH) / 2,
                 y + (KP_HEIGHT - NUM_FONT_GET_HEIGHT) / 2);
  } else {
#if 0
    lcd_drawchar(ch,
                     x + (KPF_WIDTH - FONT_WIDTH) / 2,
                     y + (KPF_HEIGHT - FONT_GET_HEIGHT) / 2);
#else
    lcd_drawchar_size(ch, x + KPF_WIDTH / 2 - FONT_WIDTH + 1, y + KPF_HEIGHT / 2 - FONT_GET_HEIGHT,
                      2);
#endif
  }
}

static void draw_keypad(void) {
  for (int i = 0; i < keypads->size; i++)
    keypad_draw_button(i);
}

static int period_pos(void) {
  int j;
  for (j = 0; kp_buf[j] && kp_buf[j] != '.'; j++)
    ;
  return j;
}

static void draw_numeric_area_frame(void) {
  lcd_set_colors(LCD_INPUT_TEXT_COLOR, LCD_INPUT_BG_COLOR);
  lcd_fill(0, LCD_HEIGHT - NUM_INPUT_HEIGHT, LCD_WIDTH, NUM_INPUT_HEIGHT);
  const char* label = keypads_mode_tbl[keypad_mode].name;
  int lines = get_lines_count(label);
  lcd_drawstring(10, LCD_HEIGHT - (FONT_STR_HEIGHT * lines + NUM_INPUT_HEIGHT) / 2, label);
}

static void draw_numeric_input(const char* buf) {
  uint16_t x = 14 + FONT_STR_WIDTH(12), space;
  uint16_t y = LCD_HEIGHT - (NUM_FONT_GET_HEIGHT + NUM_INPUT_HEIGHT) / 2;
  uint16_t xsim;
#ifdef __USE_RTC__
  if ((1 << keypad_mode) & ((1 << KM_RTC_DATE) | (1 << KM_RTC_TIME)))
    xsim = 0b01010100;
  else
#endif
    xsim = (0b00100100100100100 >> (2 - (period_pos() % 3))) & (~1);
  lcd_set_colors(LCD_INPUT_TEXT_COLOR, LCD_INPUT_BG_COLOR);
  while (*buf) {
    int c = *buf++;
    if (c == '.') {
      c = KP_PERIOD;
      xsim <<= 4;
    } else if (c == '-') {
      c = KP_MINUS;
      xsim &= ~3;
    } else if (c >= '0' && c <= '9')
      c -= '0';
    else
      continue;
    // Add space before char
    space = 2 + 10 * (xsim & 1);
    xsim >>= 1;
    lcd_fill(x, y, space, NUM_FONT_GET_HEIGHT);
    x += space;
    lcd_drawfont(c, x, y);
    x += NUM_FONT_GET_WIDTH;
  }
  lcd_fill(x, y, NUM_FONT_GET_WIDTH + 2 + 10, NUM_FONT_GET_HEIGHT);
}

static void draw_text_input(const char* buf) {
  lcd_set_colors(LCD_INPUT_TEXT_COLOR, LCD_INPUT_BG_COLOR);
#if 0
  uint16_t x = 14 + FONT_STR_WIDTH(5);
  uint16_t y = LCD_HEIGHT-(FONT_GET_HEIGHT + NUM_INPUT_HEIGHT)/2;
  lcd_fill(x, y, FONT_STR_WIDTH(20), FONT_GET_HEIGHT);
  lcd_printf(x, y, buf);
#else
  int n = 2;
  uint16_t x = 14 + FONT_STR_WIDTH(5);
  uint16_t y = LCD_HEIGHT - (FONT_GET_HEIGHT * n + NUM_INPUT_HEIGHT) / 2;
  lcd_fill(x, y, FONT_STR_WIDTH(20) * n, FONT_GET_HEIGHT * n);
  lcd_drawstring_size(buf, x, y, n);
#endif
}

//=====================================================================================================
//                                     Keyboard UI processing
//=====================================================================================================
enum { K_CONTINUE = 0, K_DONE, K_CANCEL };
static int num_keypad_click(int c, int kp_index) {
  if (c >= KP_k && c <= KP_PERCENT) {
    if (kp_index == 0)
      return K_CANCEL;
    if (c >= KP_k && c <= KP_G) { // Apply k, M, G input (add zeroes and shift . right)
      uint16_t scale = c - KP_k + 1;
      scale += (scale << 1);
      int i = period_pos();
      if (scale + i > NUMINPUT_LEN)
        scale = NUMINPUT_LEN - i;
      do {
        char v = kp_buf[i + 1];
        if (v == 0 || kp_buf[i] == 0) {
          v = '0';
          kp_buf[i + 2] = 0;
        }
        kp_buf[i + 1] = kp_buf[i];
        kp_buf[i++] = v;
      } while (--scale);
    } else if (c >= KP_m &&
               c <= KP_p) { // Apply m, u, n, p input (add format at end for atof function)
      const char prefix[] = {'m', 'u', 'n', 'p'};
      kp_buf[kp_index] = prefix[c - KP_m];
      kp_buf[kp_index + 1] = 0;
    }
    return K_DONE;
  }
#ifdef __USE_RTC__
  int maxlength = (1 << keypad_mode) & ((1 << KM_RTC_DATE) | (1 << KM_RTC_TIME)) ? 6 : NUMINPUT_LEN;
#else
  int maxlength = NUMINPUT_LEN;
#endif
  if (c == KP_BS) {
    if (kp_index == 0)
      return K_CANCEL;
    --kp_index;
  } else if (c == KP_MINUS) {
    int i;
    if (kp_buf[0] == '-') {
      for (i = 0; i < NUMINPUT_LEN; i++)
        kp_buf[i] = kp_buf[i + 1];
      --kp_index;
    } else {
      for (i = NUMINPUT_LEN; i > 0; i--)
        kp_buf[i] = kp_buf[i - 1];
      kp_buf[0] = '-';
      if (kp_index < maxlength)
        ++kp_index;
    }
  } else if (kp_index < maxlength) {
    if (c <= KP_9)
      kp_buf[kp_index++] = '0' + c;
    else if (c == KP_PERIOD && kp_index == period_pos() &&
             maxlength == NUMINPUT_LEN) // append period if there are no period and for num input
                                        // (skip for date/time)
      kp_buf[kp_index++] = '.';
  }
  kp_buf[kp_index] = '\0';
  draw_numeric_input(kp_buf);
  return K_CONTINUE;
}

static int txt_keypad_click(int c, int kp_index) {
  if (c == S_ENTER[0]) { // Enter
    return kp_index == 0 ? K_CANCEL : K_DONE;
  }
  if (c == S_LARROW[0]) { // Backspace
    if (kp_index == 0)
      return K_CANCEL;
    --kp_index;
  } else if (kp_index < TXTINPUT_LEN) { // any other text input
    kp_buf[kp_index++] = c;
  }
  kp_buf[kp_index] = '\0';
  draw_text_input(kp_buf);
  return K_CONTINUE;
}

   // We need to call a function to request normal mode.
   // ui_controller.h?
   // extern void ui_mode_normal(void); (it's static in ui_controller)
   // We should expose ui_mode_normal in ui_internal.h

// wait, ui_mode_normal is static in ui_controller.c
// I should expose it.

void ui_mode_keypad(int mode) {
  if (ui_mode == UI_KEYPAD)
    return;
  ui_mode = UI_KEYPAD;
  set_area_size(0, 0);
  // keypads array
  keypad_mode = mode;
  keypads = keypad_type_list[keypads_mode_tbl[mode].keypad_type];
  selection = -1;
  kp_buf[0] = 0;
  //  menu_draw(-1);
  draw_keypad();
  draw_numeric_area_frame();
}

static void keypad_click(int key) {
  int c = keypads->buttons[key].c; // !!! Use key + 1 (zero key index used or size define)
  int index = strlen(kp_buf);
  int result =
      keypads->type == NUM_KEYBOARD ? num_keypad_click(c, index) : txt_keypad_click(c, index);
  if (result == K_DONE)
    ui_keyboard_cb(keypad_mode, NULL); // apply input done
  // Exit loop on done or cancel
  if (result != K_CONTINUE) {
      // Switch back to normal mode
      // We need to trigger ui_controller to switch back.
      // If we expose ui_mode_normal() it solves it.
      // For now I will declare and assume I expose it.
      extern void ui_mode_normal(void);
      ui_mode_normal();
  }
}

void ui_keypad_touch(int touch_x, int touch_y) {
  const keypad_pos_t* p = &key_pos[keypads->type];
  if (touch_x < p->x_offs || touch_y < p->y_offs)
    return;
  // Calculate key position from touch x and y
  touch_x -= p->x_offs;
  touch_x /= p->width;
  touch_y -= p->y_offs;
  touch_y /= p->height;
  uint8_t pos = (touch_y & 0x0F) | (touch_x << 4);
  for (int i = 0; i < keypads->size; i++) {
    if (keypads->buttons[i].pos != pos)
      continue;
    if (keypads->buttons[i].c == KP_EMPTY)
      break;
    int old = selection;
    keypad_draw_button(selection = i); // draw new focus
    keypad_draw_button(old);           // Erase old focus
    touch_wait_release();
    selection = -1;
    keypad_draw_button(i); // erase new focus
    keypad_click(i);       // Process input
    return;
  }
  return;
}

void ui_keypad_lever(uint16_t status) {
  if (status == EVT_BUTTON_SINGLE_CLICK) {
    if (selection >= 0) // Process input
      keypad_click(selection);
    return;
  }
  int keypads_last_index = keypads->size - 1;
  do {
    int old = selection;
    do {
      if ((status & EVT_DOWN) && --selection < 0)
        selection = keypads_last_index;
      if ((status & EVT_UP) && ++selection > keypads_last_index)
        selection = 0;
    } while (keypads->buttons[selection].c == KP_EMPTY); // Skip empty
    keypad_draw_button(old);
    keypad_draw_button(selection);
    chThdSleepMilliseconds(100);
  } while ((status = ui_input_wait_release()) != 0);
}
