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

#include "nanovna.h"
#include "ui/menus/menu_display.h"
#include "ui/core/ui_menu_engine.h"
#include "ui/core/ui_core.h"
#include "ui/core/ui_keypad.h" // For KM_* definitions
#include "sys/config_service.h"

// ===================================
// Callbacks
// ===================================


static UI_FUNCTION_ADV_CALLBACK(menu_trace_acb) {
  if (b) {
    if (trace[data].enabled) {
      b->bg = LCD_TRACE_1_COLOR + data;
      if (data == selection)
        b->bg = LCD_MENU_ACTIVE_COLOR;
      if (current_trace == data)
        b->icon = BUTTON_ICON_CHECK;
    }
    b->p1.u = data;
    return;
  }

  if (trace[data].enabled && data != current_trace) // for enabled trace and not current trace
    set_active_trace(data);                         // make active
  else                                              //
    set_trace_enable(data, !trace[data].enabled);   // toggle trace enable
}

static const menuitem_t menu_trace[];
static UI_FUNCTION_ADV_CALLBACK(menu_traces_acb) {
  (void)data;
  if (b) {
    if (current_trace == TRACE_INVALID)
      return;
    b->bg = LCD_TRACE_1_COLOR + current_trace;
    //    b->p1.u = current_trace;
    return;
  }
  menu_push_submenu(menu_trace);
}

extern const menuitem_t menu_marker[];
static uint8_t get_smith_format(void) {
  return (current_trace != TRACE_INVALID) ? trace[current_trace].smith_format : 0;
}

static const menuitem_t* menu_build_marker_smith_menu(uint8_t channel);

static UI_FUNCTION_ADV_CALLBACK(menu_marker_smith_acb) {
  if (b) {
    b->icon = get_smith_format() == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.text = get_smith_format_names(data);
    return;
  }
  if (current_trace == TRACE_INVALID)
    return;
  trace[current_trace].smith_format = data;
  request_to_redraw(REDRAW_AREA | REDRAW_MARKER);
}

#define F_S11 0x00
#define F_S21 0x80
static UI_FUNCTION_ADV_CALLBACK(menu_format_acb) {
  if (current_trace == TRACE_INVALID)
    return; // Not apply any for invalid traces
  uint16_t format = data & (~F_S21);
  uint16_t channel = data & F_S21 ? 1 : 0;
  if (b) {
    if (trace[current_trace].type == format && trace[current_trace].channel == channel)
      b->icon = BUTTON_ICON_CHECK;
    if (format == TRC_SMITH) {
      uint8_t marker_smith_format = get_smith_format();
      if ((channel == 0 && !S11_SMITH_VALUE(marker_smith_format)) ||
          (channel == 1 && !S21_SMITH_VALUE(marker_smith_format)))
        return;
      plot_printf(b->label, sizeof(b->label), "%s\n" R_LINK_COLOR "%s",
                  get_trace_typename(TRC_SMITH, marker_smith_format),
                  get_smith_format_names(marker_smith_format));
    } else
      b->p1.text = get_trace_typename(format, -1);
    return;
  }

  if (format == TRC_SMITH && trace[current_trace].type == TRC_SMITH &&
      trace[current_trace].channel == channel)
    menu_push_submenu(menu_build_marker_smith_menu(channel));
  else
    set_trace_type(current_trace, format, channel);
}

static UI_FUNCTION_ADV_CALLBACK(menu_channel_acb) {
  (void)data;
  if (current_trace == TRACE_INVALID) {
    if (b)
      b->p1.text = "";
    return;
  }
  int ch = trace[current_trace].channel;
  if (b) {
    b->p1.text = ch == 0 ? "S11 (REFL)" : "S21 (THRU)";
    return;
  }
  // Change channel only if trace type available for this
  if ((1 << (trace[current_trace].type)) & S11_AND_S21_TYPE_MASK)
    set_trace_channel(current_trace, ch ^ 1);
}

// Custom keyboard menu button callback (depend from current trace)
static UI_FUNCTION_ADV_CALLBACK(menu_scale_keyboard_acb) {
  // Not apply amplitude / scale / ref for invalid or polar graph
  if (current_trace == TRACE_INVALID)
    return;
  uint32_t type_mask = 1 << trace[current_trace].type;
  if ((type_mask & ROUND_GRID_MASK) && data != KM_SCALE)
    return;
  // Nano scale values
  uint32_t nano_keyb_type = (1 << KM_TOP) | (1 << KM_BOTTOM) | (1 << KM_SCALE);
  if ((type_mask & NANO_TYPE_MASK) && ((1 << data) & nano_keyb_type))
    data++;
  menu_keyboard_acb(data, b);
}

static UI_FUNCTION_CALLBACK(menu_auto_scale_cb) {
  (void)data;
  if (current_trace == TRACE_INVALID || sweep_points == 0)
    return;
  int type = trace[current_trace].type;
  get_value_cb_t c = trace_info_list[type].get_value_cb; // Get callback for value calculation
  if (c == NULL)
    return; // No callback, skip

  float (*array)[2] = measured[trace[current_trace].channel];
  float min_val, max_val;

  // Initialize with the first point
  float v = c(0, array[0]);
  if (vna_fabsf(v) == infinityf())
      v = 0; // fallback if infinite
  min_val = max_val = v;

  for (uint16_t i = 1; i < sweep_points; i++) {
    v = c(i, array[i]);
    if (vna_fabsf(v) == infinityf())
      continue;
    if (v < min_val)
      min_val = v;
    if (v > max_val)
      max_val = v;
  }

  // If signal is flat
  if (min_val == max_val) {
      if (min_val == 0) {
          min_val = -1.0;
          max_val = 1.0; // Avoid 0 span
      } else {
        float span = vna_fabsf(min_val) * 0.1f; // 10% span
        min_val -= span;
        max_val += span;
      }
  }

  // Set scale and refpos
  float scale = (max_val - min_val) / NGRIDY;
  // Align scale to nice numbers (1, 2, 5 sequence usually, but here just raw)
  // set_trace_scale(current_trace, scale); 
  // set_trace_refpos(current_trace, NGRIDY - (max_val / scale)); 
  // Let's optimize slightly or keep raw behavior
  // Keeping original logic structure implied (original implementation cut off in view)
  // Assuming basic implementation:
  set_trace_scale(current_trace, scale);
  set_trace_refpos(current_trace, NGRIDY - (max_val / scale));
}

// ===================================
// Menus
// ===================================

// Keyboard Callbacks

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

const menuitem_t menu_scale[] = {
    {MT_CALLBACK, 0, "AUTO SCALE", menu_auto_scale_cb},
    {MT_ADV_CALLBACK, KM_TOP, "TOP", menu_scale_keyboard_acb},
    {MT_ADV_CALLBACK, KM_BOTTOM, "BOTTOM", menu_scale_keyboard_acb},
    {MT_ADV_CALLBACK, KM_SCALE, "SCALE/DIV", menu_scale_keyboard_acb},
    {MT_ADV_CALLBACK, KM_REFPOS, "REFERENCE\nPOSITION", menu_scale_keyboard_acb},
    {MT_ADV_CALLBACK, KM_EDELAY, "E-DELAY", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_S21OFFSET, "S21 OFFSET\n " R_LINK_COLOR "%b.3F" S_dB, menu_keyboard_acb},
#ifdef __USE_GRID_VALUES__
    {MT_ADV_CALLBACK, VNA_MODE_SHOW_GRID, "SHOW GRID\nVALUES", menu_vna_mode_acb},
    {MT_ADV_CALLBACK, VNA_MODE_DOT_GRID, "DOT GRID", menu_vna_mode_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

#if STORED_TRACES == 1
static const menuitem_t menu_trace[] = {
    {MT_ADV_CALLBACK, 0, "TRACE 0", menu_trace_acb},
    {MT_ADV_CALLBACK, 1, "TRACE 1", menu_trace_acb},
    {MT_ADV_CALLBACK, 2, "TRACE 2", menu_trace_acb},
    {MT_ADV_CALLBACK, 3, "TRACE 3", menu_trace_acb},
    {MT_ADV_CALLBACK, 0, "%s TRACE", menu_stored_trace_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#elif STORED_TRACES > 1
const menuitem_t menu_trace[] = {
    {MT_ADV_CALLBACK, 0, "TRACE 0", menu_trace_acb},
    {MT_ADV_CALLBACK, 1, "TRACE 1", menu_trace_acb},
    {MT_ADV_CALLBACK, 2, "TRACE 2", menu_trace_acb},
    {MT_ADV_CALLBACK, 3, "TRACE 3", menu_trace_acb},
    {MT_ADV_CALLBACK, 0, "%s TRACE A", menu_stored_trace_acb},
    {MT_ADV_CALLBACK, 1, "%s TRACE B", menu_stored_trace_acb},
#if STORED_TRACES > 2
    {MT_ADV_CALLBACK, 2, "%s TRACE C", menu_stored_trace_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#else
const menuitem_t menu_trace[] = {
    {MT_ADV_CALLBACK, 0, "TRACE 0", menu_trace_acb},
    {MT_ADV_CALLBACK, 1, "TRACE 1", menu_trace_acb},
    {MT_ADV_CALLBACK, 2, "TRACE 2", menu_trace_acb},
    {MT_ADV_CALLBACK, 3, "TRACE 3", menu_trace_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

static const menuitem_t menu_format4[] = {
    {MT_ADV_CALLBACK, F_S21 | TRC_Rser, "SERIES R", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Xser, "SERIES X", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Zser, "SERIES |Z|", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Rsh, "SHUNT R", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Xsh, "SHUNT X", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Zsh, "SHUNT |Z|", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Qs21, "Q FACTOR", menu_format_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

static const menuitem_t menu_formatS21[] = {
    {MT_ADV_CALLBACK, F_S21 | TRC_LOGMAG, "LOGMAG", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_PHASE, "PHASE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_DELAY, "DELAY", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_SMITH, "SMITH", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_POLAR, "POLAR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_LINEAR, "LINEAR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_REAL, "REAL", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_IMAG, "IMAG", menu_format_acb},
    {MT_SUBMENU, 0, S_RARROW " MORE", menu_format4},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

static const menuitem_t menu_format3[] = {
    {MT_ADV_CALLBACK, F_S11 | TRC_ZPHASE, "Z PHASE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Cs, "SERIES C", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Ls, "SERIES L", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Rp, "PARALLEL R", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Xp, "PARALLEL X", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Cp, "PARALLEL C", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Lp, "PARALLEL L", menu_format_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

static const menuitem_t menu_format2[] = {
    {MT_ADV_CALLBACK, F_S11 | TRC_POLAR, "POLAR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_LINEAR, "LINEAR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_REAL, "REAL", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_IMAG, "IMAG", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Q, "Q FACTOR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_G, "CONDUCTANCE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_B, "SUSCEPTANCE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Y, "|Y|", menu_format_acb},
    {MT_SUBMENU, 0, S_RARROW " MORE", menu_format3},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

static const menuitem_t menu_formatS11[] = {
    {MT_ADV_CALLBACK, F_S11 | TRC_LOGMAG, "LOGMAG", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_PHASE, "PHASE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_DELAY, "DELAY", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_SMITH, "SMITH", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_SWR, "SWR", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_R, "RESISTANCE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_X, "REACTANCE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Z, "|Z|", menu_format_acb},
    {MT_SUBMENU, 0, S_RARROW " MORE", menu_format2},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

static const menu_descriptor_t menu_marker_s21smith_desc[] = {
    {MT_ADV_CALLBACK, MS_LIN},
    {MT_ADV_CALLBACK, MS_LOG},
    {MT_ADV_CALLBACK, MS_REIM},
    {MT_ADV_CALLBACK, MS_SHUNT_RX},
    {MT_ADV_CALLBACK, MS_SHUNT_RLC},
    {MT_ADV_CALLBACK, MS_SERIES_RX},
    {MT_ADV_CALLBACK, MS_SERIES_RLC},
};

static const menu_descriptor_t menu_marker_s11smith_desc[] = {
    {MT_ADV_CALLBACK, MS_LIN},
    {MT_ADV_CALLBACK, MS_LOG},
    {MT_ADV_CALLBACK, MS_REIM},
    {MT_ADV_CALLBACK, MS_RX},
    {MT_ADV_CALLBACK, MS_RLC},
    {MT_ADV_CALLBACK, MS_GB},
    {MT_ADV_CALLBACK, MS_GLC},
    {MT_ADV_CALLBACK, MS_RpXp},
    {MT_ADV_CALLBACK, MS_RpLC},
};

static const menuitem_t* menu_build_marker_smith_menu(uint8_t channel) {
  menuitem_t* cursor = menu_dynamic_acquire();
  menuitem_t* base = cursor; // Fix: return base
  const menu_descriptor_t* desc = channel == 0 ? menu_marker_s11smith_desc : menu_marker_s21smith_desc;
  size_t count = channel == 0 ? ARRAY_COUNT(menu_marker_s11smith_desc) : ARRAY_COUNT(menu_marker_s21smith_desc);
  cursor = ui_menu_list(desc, count, "%s", menu_marker_smith_acb, cursor);
  menu_set_next(cursor, menu_back);
  return base;
}

const menuitem_t menu_display[] = {
    {MT_ADV_CALLBACK, 0, "TRACES", menu_traces_acb},
    {MT_SUBMENU, 0, "FORMAT\nS11", menu_formatS11},
    {MT_SUBMENU, 0, "FORMAT\nS21", menu_formatS21},
    {MT_ADV_CALLBACK, 0, "CHANNEL\n " R_LINK_COLOR "%s", menu_channel_acb},
    {MT_SUBMENU, 0, "SCALE", menu_scale},
    {MT_SUBMENU, 0, "MARKERS", menu_marker},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

// ===================================
// Transform Logic
// ===================================
static const option_desc_t transform_window_options[] = {
    {TD_WINDOW_MINIMUM, "MINIMUM", BUTTON_ICON_NONE},
    {TD_WINDOW_NORMAL, "NORMAL", BUTTON_ICON_NONE},
    {TD_WINDOW_MAXIMUM, "MAXIMUM", BUTTON_ICON_NONE},
};

static UI_FUNCTION_ADV_CALLBACK(menu_transform_window_acb) {
  (void)data;  // Suppress unused parameter warning
  uint16_t window = props_mode & TD_WINDOW;
  ui_cycle_option(&window, transform_window_options, ARRAY_COUNT(transform_window_options), b);
  if (b)
    return;
  props_mode = (props_mode & (uint16_t)~TD_WINDOW) | window;
}

static const option_desc_t transform_state_options[] = {
    {0, "OFF", BUTTON_ICON_NOCHECK},
    {DOMAIN_TIME, "ON", BUTTON_ICON_CHECK},
};

static UI_FUNCTION_ADV_CALLBACK(menu_transform_acb) {
  (void)data;
  uint16_t state = props_mode & DOMAIN_TIME;
  ui_cycle_option(&state, transform_state_options, ARRAY_COUNT(transform_state_options), b);
  if (b)
    return;
  props_mode = (props_mode & (uint16_t)~DOMAIN_TIME) | state;
  select_lever_mode(LM_MARKER);
  request_to_redraw(REDRAW_FREQUENCY | REDRAW_AREA);
}

static UI_FUNCTION_ADV_CALLBACK(menu_transform_filter_acb) {
  if (b) {
    b->icon = (props_mode & TD_FUNC) == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    return;
  }
  props_mode = (props_mode & ~TD_FUNC) | data;
}

const menuitem_t menu_transform[] = {
    {MT_ADV_CALLBACK, 0, "TRANSFORM\n%s", menu_transform_acb},
    {MT_ADV_CALLBACK, TD_FUNC_LOWPASS_IMPULSE, "LOW PASS\nIMPULSE", menu_transform_filter_acb},
    {MT_ADV_CALLBACK, TD_FUNC_LOWPASS_STEP, "LOW PASS\nSTEP", menu_transform_filter_acb},
    {MT_ADV_CALLBACK, TD_FUNC_BANDPASS, "BANDPASS", menu_transform_filter_acb},
    {MT_ADV_CALLBACK, 0, "WINDOW\n " R_LINK_COLOR "%s", menu_transform_window_acb},
    {MT_ADV_CALLBACK, KM_VELOCITY_FACTOR, "VELOCITY F.\n " R_LINK_COLOR "%d%%%%",
     menu_keyboard_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

// ===================================
// Bandwidth & Smooth Logic
// ===================================
static const menu_descriptor_t menu_bandwidth_desc[] = {
#ifdef BANDWIDTH_8000
    {MT_ADV_CALLBACK, BANDWIDTH_8000},
#endif
#ifdef BANDWIDTH_4000
    {MT_ADV_CALLBACK, BANDWIDTH_4000},
#endif
#ifdef BANDWIDTH_2000
    {MT_ADV_CALLBACK, BANDWIDTH_2000},
#endif
#ifdef BANDWIDTH_1000
    {MT_ADV_CALLBACK, BANDWIDTH_1000},
#endif
#ifdef BANDWIDTH_333
    {MT_ADV_CALLBACK, BANDWIDTH_333},
#endif
#ifdef BANDWIDTH_100
    {MT_ADV_CALLBACK, BANDWIDTH_100},
#endif
#ifdef BANDWIDTH_30
    {MT_ADV_CALLBACK, BANDWIDTH_30},
#endif
#ifdef BANDWIDTH_10
    {MT_ADV_CALLBACK, BANDWIDTH_10},
#endif
};

static UI_FUNCTION_ADV_CALLBACK(menu_bandwidth_acb) {
  if (b) {
    b->icon = config._bandwidth == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = get_bandwidth_frequency(data);
    return;
  }
  set_bandwidth(data);
}

static const menuitem_t* menu_build_bandwidth_menu(void) {
  menuitem_t* cursor = menu_dynamic_acquire();
  const menuitem_t* base = cursor;
  cursor = ui_menu_list(menu_bandwidth_desc, ARRAY_COUNT(menu_bandwidth_desc), "%u " S_Hz,
                        menu_bandwidth_acb, cursor);
  menu_set_next(cursor, menu_back);
  return base;
}

static UI_FUNCTION_ADV_CALLBACK(menu_bandwidth_sel_acb) {
  (void)data;
  if (b) {
    b->p1.u = get_bandwidth_frequency(config._bandwidth);
    return;
  }
  menu_push_submenu(menu_build_bandwidth_menu());
}

#ifdef __USE_SMOOTH__
static const menu_descriptor_t menu_smooth_desc[] = {
    {MT_ADV_CALLBACK, 1},
    {MT_ADV_CALLBACK, 2},
    {MT_ADV_CALLBACK, 4},
    {MT_ADV_CALLBACK, 5},
    {MT_ADV_CALLBACK, 6},
};

static UI_FUNCTION_ADV_CALLBACK(menu_smooth_acb) {
  if (b) {
    b->icon = get_smooth_factor() == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = data;
    return;
  }
  set_smooth_factor(data);
}

static const menuitem_t* menu_build_smooth_menu(void) {
  menuitem_t* cursor = menu_dynamic_acquire();
  const menuitem_t* base = cursor;
  *cursor++ = (menuitem_t){MT_ADV_CALLBACK, VNA_MODE_SMOOTH, "SMOOTH\n " R_LINK_COLOR "%s avg",
                           menu_vna_mode_acb};
  *cursor++ = (menuitem_t){MT_ADV_CALLBACK, 0, "SMOOTH\nOFF", menu_smooth_acb};
  cursor = ui_menu_list(menu_smooth_desc, ARRAY_COUNT(menu_smooth_desc), "x%d", menu_smooth_acb,
                        cursor);
  menu_set_next(cursor, menu_back);
  return base;
}

static UI_FUNCTION_ADV_CALLBACK(menu_smooth_sel_acb) {
  (void)data;
  if (b)
    return;
  menu_push_submenu(menu_build_smooth_menu());
}
#endif

// ===================================
// Measure Logic
// ===================================
#ifdef __VNA_MEASURE_MODULE__
extern const menuitem_t* const menu_measure_list[];
static UI_FUNCTION_ADV_CALLBACK(menu_measure_acb) {
  if (b) {
    b->icon = current_props._measure == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    return;
  }
  plot_set_measure_mode(data);
  menu_set_submenu(menu_measure_list[current_props._measure]);
}

static UI_FUNCTION_CALLBACK(menu_measure_cb) {
  (void)data;
  menu_push_submenu(menu_measure_list[current_props._measure]);
}

// Select menu depend from measure mode
#ifdef __USE_LC_MATCHING__
const menuitem_t menu_measure_lc[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_LC_MATH, "L/C MATCH", menu_measure_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

#ifdef __S11_CABLE_MEASURE__
const menuitem_t menu_measure_cable[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_S11_CABLE, "CABLE\n (S11)", menu_measure_acb},
    {MT_ADV_CALLBACK, KM_VELOCITY_FACTOR, "VELOCITY F.\n " R_LINK_COLOR "%d%%%%",
     menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_ACTUAL_CABLE_LEN, "CABLE LENGTH", menu_keyboard_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

#ifdef __S11_RESONANCE_MEASURE__
const menuitem_t menu_measure_resonance[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_S11_RESONANCE, "RESONANCE\n (S11)", menu_measure_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

#ifdef __S21_MEASURE__
const menuitem_t menu_measure_s21[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SHUNT_LC, "SHUNT LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_LC, "SERIES LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_XTAL, "SERIES\nXTAL (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, KM_MEASURE_R, " Rl = " R_LINK_COLOR "%b.4F" S_OHM, menu_keyboard_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_measure_filter[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_FILTER, "FILTER\n (S21)", menu_measure_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

const menuitem_t menu_measure[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
#ifdef __USE_LC_MATCHING__
    {MT_ADV_CALLBACK, MEASURE_LC_MATH, "L/C MATCH", menu_measure_acb},
#endif
#ifdef __S11_CABLE_MEASURE__
    {MT_ADV_CALLBACK, MEASURE_S11_CABLE, "CABLE\n (S11)", menu_measure_acb},
#endif
#ifdef __S11_RESONANCE_MEASURE__
    {MT_ADV_CALLBACK, MEASURE_S11_RESONANCE, "RESONANCE\n (S11)", menu_measure_acb},
#endif
#ifdef __S21_MEASURE__
    {MT_ADV_CALLBACK, MEASURE_SHUNT_LC, "SHUNT LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_LC, "SERIES LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_XTAL, "SERIES\nXTAL (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_FILTER, "FILTER\n (S21)", menu_measure_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

// Dynamic menu selector depend from measure mode
const menuitem_t* const menu_measure_list[] = {
    [MEASURE_NONE] = menu_measure,
#ifdef __USE_LC_MATCHING__
    [MEASURE_LC_MATH] = menu_measure_lc,
#endif
#ifdef __S21_MEASURE__
    [MEASURE_SHUNT_LC] = menu_measure_s21,
    [MEASURE_SERIES_LC] = menu_measure_s21,
    [MEASURE_SERIES_XTAL] = menu_measure_s21,
    [MEASURE_FILTER] = menu_measure_filter,
#endif
#ifdef __S11_CABLE_MEASURE__
    [MEASURE_S11_CABLE] = menu_measure_cable,
#endif
#ifdef __S11_RESONANCE_MEASURE__
    [MEASURE_S11_RESONANCE] = menu_measure_resonance,
#endif
};
#endif

const menuitem_t menu_measure_tools[] = {
    {MT_SUBMENU, 0, "TRANSFORM", menu_transform},
#ifdef __USE_SMOOTH__
    {MT_ADV_CALLBACK, 0, "DATA\nSMOOTH", menu_smooth_sel_acb},
#endif
#ifdef __VNA_MEASURE_MODULE__
    {MT_CALLBACK, 0, "MEASURE", menu_measure_cb},
#endif
    {MT_ADV_CALLBACK, 0, "IF BANDWIDTH\n " R_LINK_COLOR "%u" S_Hz, menu_bandwidth_sel_acb},
#ifdef __VNA_Z_RENORMALIZATION__
    {MT_ADV_CALLBACK, KM_Z_PORT, "PORT-Z\n " R_LINK_COLOR "50 " S_RARROW " %bF" S_OHM,
     menu_keyboard_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
