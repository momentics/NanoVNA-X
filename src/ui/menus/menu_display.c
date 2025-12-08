#include "nanovna.h"
#include "hal.h"
#include "ui/menus/menu_internal.h"
#include "chprintf.h"
#include "ui/input/ui_touch.h"
#include "ui/input/ui_keypad.h"

#define F_S11 0x00
#define F_S21 0x80

// Forward declaration
extern const menuitem_t menu_trace[];

static UI_FUNCTION_ADV_CALLBACK(menu_trace_acb) {
  if (b) {
    if (trace[data].enabled) {
      b->bg = LCD_TRACE_1_COLOR + data;
      if (data == selection)
        b->bg = LCD_MENU_ACTIVE_COLOR;
      if (current_trace == data)
        b->icon = BUTTON_ICON_CHECK;
      // b->icon = trace[data].enabled ? BUTTON_ICON_CHECK : BUTTON_ICON_NOCHECK;
      plot_printf(b->label, sizeof(b->label), "TRACE %d\n%s", data,
                  get_trace_typename(trace[data].type, 0xf));
    } else {
      plot_printf(b->label, sizeof(b->label), "TRACE %d", data);
    }
    return;
  }
  // trace[data].enabled = !trace[data].enabled;
  if (current_trace != data) {
    current_trace = data;
    // update_trace_buffer(data);
    plot_init();
  } else {
    // toggle trace
    trace[data].enabled = !trace[data].enabled;
  }
}

static UI_FUNCTION_ADV_CALLBACK(menu_traces_acb) {
  (void)data;
  (void)b;
  menu_push_submenu(menu_trace);
}

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

      if (trace[current_trace].type == format && trace[current_trace].channel == channel)
        b->icon = BUTTON_ICON_CHECK;
      else
        b->icon = BUTTON_ICON_NOCHECK;
      return;
    }
    return;
  }
  if (format == TRC_SMITH && trace[current_trace].type == TRC_SMITH &&
      trace[current_trace].channel == channel)
    menu_push_submenu(menu_build_marker_smith_menu(channel));
  else
    set_trace_type(current_trace, format, channel);
}

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
  ui_keyboard_cb(data, b);
}

static UI_FUNCTION_ADV_CALLBACK(menu_stored_trace_acb) {
  if (b) {
    if (trace[data].enabled) {
      if (trace[data].type == (uint8_t)TRACE_INVALID) {
        b->bg = LCD_MENU_COLOR;
        b->icon = BUTTON_ICON_NOCHECK;
      } else {
        b->bg = LCD_TRACE_1_COLOR + data;
        b->icon = BUTTON_ICON_CHECK;
      }
      if (data == selection)
        b->bg = LCD_MENU_ACTIVE_COLOR;
      plot_printf(b->label, sizeof(b->label), "%s TRACE",
                  data == 0 ? "MEMORY" : (data == STORED_TRACES ? "DATA" : ""));
    } else {
      plot_printf(b->label, sizeof(b->label), "TRACE %d", data);
    }
    return;
  }
}

const menuitem_t menu_trace[] = {
    {MT_ADV_CALLBACK, 0, "TRACE %d", menu_trace_acb},
    {MT_ADV_CALLBACK, 1, "TRACE %d", menu_trace_acb},
    {MT_ADV_CALLBACK, 2, "TRACE %d", menu_trace_acb},
    {MT_ADV_CALLBACK, 3, "TRACE %d", menu_trace_acb},
#if STORED_TRACES == 1
    {MT_ADV_CALLBACK, 0, "%s TRACE", menu_stored_trace_acb},
#elif STORED_TRACES > 1
    {MT_ADV_CALLBACK, 0, "%s TRACE A", menu_stored_trace_acb},
    {MT_ADV_CALLBACK, 1, "%s TRACE B", menu_stored_trace_acb},
#if STORED_TRACES > 2
    {MT_ADV_CALLBACK, 2, "%s TRACE C", menu_stored_trace_acb},
#endif
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};


const menuitem_t menu_format4[] = {
    {MT_ADV_CALLBACK, F_S21 | TRC_Rser, "SERIES R", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Xser, "SERIES X", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Zser, "SERIES |Z|", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Rsh, "SHUNT R", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Xsh, "SHUNT X", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Zsh, "SHUNT |Z|", menu_format_acb},
    {MT_ADV_CALLBACK, F_S21 | TRC_Qs21, "Q FACTOR", menu_format_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_formatS21[] = {
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

const menuitem_t menu_format3[] = {
    {MT_ADV_CALLBACK, F_S11 | TRC_ZPHASE, "Z PHASE", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Cs, "SERIES C", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Ls, "SERIES L", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Rp, "PARALLEL R", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Xp, "PARALLEL X", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Cp, "PARALLEL C", menu_format_acb},
    {MT_ADV_CALLBACK, F_S11 | TRC_Lp, "PARALLEL L", menu_format_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_format2[] = {
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

const menuitem_t menu_formatS11[] = {
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

const menuitem_t menu_scale[] = {
    {MT_CALLBACK, 0, "AUTO SCALE", menu_auto_scale_cb},
    {MT_ADV_CALLBACK, KM_TOP, "TOP", menu_scale_keyboard_acb},
    {MT_ADV_CALLBACK, KM_BOTTOM, "BOTTOM", menu_scale_keyboard_acb},
    {MT_ADV_CALLBACK, KM_SCALE, "SCALE/DIV", menu_scale_keyboard_acb},
    {MT_ADV_CALLBACK, KM_REFPOS, "REFERENCE\nPOSITION", menu_scale_keyboard_acb},
#if defined(NANOVNA_F103) || defined(NANOVNA_F303)
    // STM32F072 does not have enough flash
    {MT_ADV_CALLBACK, KM_EDELAY, "E-DELAY", menu_keyboard_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};


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

const menuitem_t menu_display[] = {
    {MT_ADV_CALLBACK, 0, "TRACES", menu_traces_acb},
    {MT_SUBMENU, 0, "FORMAT\nS11", menu_formatS11},
    {MT_SUBMENU, 0, "FORMAT\nS21", menu_formatS21},
    {MT_ADV_CALLBACK, 0, "CHANNEL\n " R_LINK_COLOR "%s", menu_channel_acb},
    {MT_SUBMENU, 0, "SCALE", menu_scale},
    {MT_SUBMENU, 0, "MARKERS", menu_marker},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

