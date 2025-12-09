#include "nanovna.h"
#include "ui/menus/menu_display.h"
#include "ui/core/ui_menu_engine.h"
#include "ui/core/ui_core.h"
#include "ui/core/ui_keypad.h"

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
