#include "nanovna.h"
#include "ui/menus/menu_cal.h"
#include "ui/core/ui_menu_engine.h"
#include "ui/core/ui_core.h"
#include "ui/core/ui_keypad.h"
#include "infra/storage/config_service.h"
#include "platform/peripherals/si5351.h"
#include "core/config_macros.h"

// ===================================
// Descriptors
// ===================================

static const menu_descriptor_t MENU_STATE_SLOTS_DESC[] = {
  {MT_ADV_CALLBACK, 0}, {MT_ADV_CALLBACK, 1}, {MT_ADV_CALLBACK, 2},
#if SAVEAREA_MAX > 3
  {MT_ADV_CALLBACK, 3},
#endif
#if SAVEAREA_MAX > 4
  {MT_ADV_CALLBACK, 4},
#endif
#if SAVEAREA_MAX > 5
  {MT_ADV_CALLBACK, 5},
#endif
#if SAVEAREA_MAX > 6
  {MT_ADV_CALLBACK, 6},
#endif
};

static const menu_descriptor_t MENU_POWER_DESC[] = {
  {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_2MA},
  {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_4MA},
  {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_6MA},
  {MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_8MA},
};

// ===================================
// Callbacks
// ===================================

static UI_FUNCTION_ADV_CALLBACK(menu_calop_acb) {
  static const struct {
    uint8_t mask, next;
  } c_list[5] = {
    [CAL_LOAD] = {CALSTAT_LOAD, 3},   [CAL_OPEN] = {CALSTAT_OPEN, 1},
    [CAL_SHORT] = {CALSTAT_SHORT, 2}, [CAL_THRU] = {CALSTAT_THRU, 6},
    [CAL_ISOLN] = {CALSTAT_ISOLN, 4},
  };
  if (b) {
    if (cal_status & c_list[data].mask)
      b->icon = BUTTON_ICON_CHECK;
    return;
  }
  // Reset the physical button debounce state when advancing through CAL steps
  ui_input_reset_state();
  cal_collect(data);
  selection = c_list[data].next;
}

static UI_FUNCTION_ADV_CALLBACK(menu_cal_enh_acb) {
  (void)data;
  if (b) {
    b->icon = (cal_status & CALSTAT_ENHANCED_RESPONSE) ? BUTTON_ICON_CHECK : BUTTON_ICON_NOCHECK;
    return;
  }
  // toggle applying correction
  cal_status ^= CALSTAT_ENHANCED_RESPONSE;
  request_to_redraw(REDRAW_CAL_STATUS);
}

static UI_FUNCTION_CALLBACK(menu_caldone_cb) {
  // Indicate that calibration is in progress
  calibration_in_progress = true;

  // Complete calibration without interfering with measurements
  cal_done();

  // Reset the flag (cal_done also sets it to false)
  calibration_in_progress = false;

  // For both cases, simply return to the parent menu
  menu_move_back(false);
  if (data)
    ui_mode_normal(); // DONE IN RAM
}

static UI_FUNCTION_CALLBACK(menu_cal_reset_cb) {
  (void)data;
  // RESET
  cal_status &= CALSTAT_ENHANCED_RESPONSE; // leave ER state
  lastsaveid = NO_SAVE_SLOT;
  // set_power(SI5351_CLK_DRIVE_STRENGTH_AUTO);
  request_to_redraw(REDRAW_CAL_STATUS);
}

static UI_FUNCTION_ADV_CALLBACK(menu_cal_range_acb) {
  (void)data;
  bool calibrated = cal_status & (CALSTAT_ES | CALSTAT_ER | CALSTAT_ET | CALSTAT_ED | CALSTAT_EX |
                                  CALSTAT_OPEN | CALSTAT_SHORT | CALSTAT_THRU);
  if (!calibrated)
    return;
  if (b) {
    b->bg = (cal_status & CALSTAT_INTERPOLATED) ? LCD_INTERP_CAL_COLOR : LCD_MENU_COLOR;
    plot_printf(b->label, sizeof(b->label), "CAL: %dp\n %.6F" S_HZ "\n %.6F" S_HZ, cal_sweep_points,
                (float)cal_frequency0, (float)cal_frequency1);
    return;
  }
  // Reset range to calibration
  if (cal_status & CALSTAT_INTERPOLATED) {
    reset_sweep_frequency();
    set_power(cal_power);
  }
}

static UI_FUNCTION_ADV_CALLBACK(menu_cal_apply_acb) {
  (void)data;
  if (b) {
    b->icon = (cal_status & CALSTAT_APPLY) ? BUTTON_ICON_CHECK : BUTTON_ICON_NOCHECK;
    return;
  }
  // toggle applying correction
  cal_status ^= CALSTAT_APPLY;
  request_to_redraw(REDRAW_CAL_STATUS);
}

static UI_FUNCTION_ADV_CALLBACK(menu_power_acb) {
  if (b) {
    b->icon = current_props._power == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    return;
  }
  set_power(data);
}

static const menuitem_t *menu_build_power_menu(void) {
  menuitem_t *cursor = menu_dynamic_acquire();
  menuitem_t *base = cursor;
  *cursor++ = (menuitem_t){MT_ADV_CALLBACK, SI5351_CLK_DRIVE_STRENGTH_AUTO, "AUTO", menu_power_acb};
  cursor = ui_menu_list(MENU_POWER_DESC, ARRAY_COUNT(MENU_POWER_DESC), "%u m" S_AMPER,
                        menu_power_acb, cursor);
  menu_set_next(cursor, MENU_BACK);
  return base;
}

static UI_FUNCTION_ADV_CALLBACK(menu_power_sel_acb) {
  (void)data;
  if (b) {
    if (current_props._power != SI5351_CLK_DRIVE_STRENGTH_AUTO) {
      plot_printf(b->label, sizeof(b->label), "POWER" R_LINK_COLOR "  %um" S_AMPER,
                  2 + current_props._power * 2);
    }
    return;
  }
  menu_push_submenu(menu_build_power_menu());
}

static UI_FUNCTION_ADV_CALLBACK(menu_recall_acb) {
  if (b) {
    const properties_t *p = get_properties(data);
    if (p) {
      plot_printf(b->label, sizeof(b->label), "%.6F" S_HZ "\n%.6F" S_HZ, (float)p->_frequency0,
                  (float)p->_frequency1);
    } else {
      b->p1.u = data;
    }
    if (lastsaveid == data)
      b->icon = BUTTON_ICON_CHECK;
    return;
  }

  load_properties(data);
}

static const menuitem_t *menu_build_recall_menu(void) {
  menuitem_t *cursor = menu_dynamic_acquire();
  menuitem_t *base = cursor;
#ifdef SD_FILE_BROWSER
//   *cursor++ = (menuitem_t){MT_CALLBACK, FMT_CAL_FILE, "LOAD FROM\n SD CARD",
//                            menu_sdcard_browse_cb}; // Requires menu_sdcard_browse_cb exposure
#endif
  cursor = ui_menu_list(MENU_STATE_SLOTS_DESC, ARRAY_COUNT(MENU_STATE_SLOTS_DESC), "Empty %d",
                        menu_recall_acb, cursor);
  menu_set_next(cursor, MENU_BACK);
  return base;
}

static UI_FUNCTION_CALLBACK(menu_recall_submenu_cb) {
  (void)data;
  menu_push_submenu(menu_build_recall_menu());
}

static UI_FUNCTION_ADV_CALLBACK(menu_save_acb) {
  if (b) {
    const properties_t *p = get_properties(data);
    if (p) {
      plot_printf(b->label, sizeof(b->label), "%.6F" S_HZ "\n%.6F" S_HZ, (float)p->_frequency0,
                  (float)p->_frequency1);
    } else {
      b->p1.u = data;
    }
    if (lastsaveid == data)
      b->icon = BUTTON_ICON_CHECK;
    return;
  }

  int result = caldata_save(data);

  if (result == 0) {
    menu_move_back(true);
    request_to_redraw(REDRAW_BACKUP | REDRAW_CAL_STATUS);
  } else {
    // Show error if save failed
    ui_message_box("SAVE ERROR", "Failed to save calibration", 1000);
  }
}

static const menuitem_t *menu_build_save_menu(void) {
  menuitem_t *cursor = menu_dynamic_acquire();
  menuitem_t *base = cursor;
#ifdef SD_FILE_BROWSER
  *cursor++ = (menuitem_t){MT_CALLBACK, FMT_CAL_FILE, "SAVE TO\n SD CARD", menu_sdcard_cb};
#endif
  cursor = ui_menu_list(MENU_STATE_SLOTS_DESC, ARRAY_COUNT(MENU_STATE_SLOTS_DESC), "Empty %d",
                        menu_save_acb, cursor);
  menu_set_next(cursor, MENU_BACK);
  return base;
}

static UI_FUNCTION_CALLBACK(menu_save_submenu_cb) {
  (void)data;
  menu_push_submenu(menu_build_save_menu());
}

// ===================================
// Menus
// ===================================

const menuitem_t MENU_STATE_IO[] = {
  {MT_CALLBACK, 0, "SAVE CAL", menu_save_submenu_cb},
  {MT_CALLBACK, 0, "RECALL CAL", menu_recall_submenu_cb},
  {MT_NEXT, 0, NULL, MENU_BACK} // next-> MENU_BACK
};

const menuitem_t MENU_CAL_WIZARD[] = {
  {MT_ADV_CALLBACK, CAL_OPEN, "OPEN", menu_calop_acb},
  {MT_ADV_CALLBACK, CAL_SHORT, "SHORT", menu_calop_acb},
  {MT_ADV_CALLBACK, CAL_LOAD, "LOAD", menu_calop_acb},
  {MT_ADV_CALLBACK, CAL_ISOLN, "ISOLN", menu_calop_acb},
  {MT_ADV_CALLBACK, CAL_THRU, "THRU", menu_calop_acb},
  {MT_CALLBACK, 0, "DONE", menu_caldone_cb},
  {MT_CALLBACK, 1, "DONE IN RAM", menu_caldone_cb},
  {MT_NEXT, 0, NULL, MENU_BACK} // next-> MENU_BACK
};

const menuitem_t MENU_CAL_OPTIONS[] = {
  {MT_ADV_CALLBACK, 0, "CAL RANGE", menu_cal_range_acb},
  {MT_ADV_CALLBACK, 0, "CAL POWER", menu_power_sel_acb},
  {MT_ADV_CALLBACK, 0, "ENHANCED\nRESPONSE", menu_cal_enh_acb},
#ifdef VNA_Z_RENORMALIZATION
  {MT_ADV_CALLBACK, KM_CAL_LOAD_R, "LOAD STD\n " R_LINK_COLOR "%bF" S_OHM, menu_keyboard_acb},
#endif
  {MT_NEXT, 0, NULL, MENU_BACK} // next-> MENU_BACK
};

const menuitem_t MENU_CAL_MANAGEMENT[] = {
  {MT_ADV_CALLBACK, 0, "CAL APPLY", menu_cal_apply_acb},
  {MT_CALLBACK, 0, "CAL RESET", menu_cal_reset_cb},
  {MT_NEXT, 0, NULL, MENU_BACK} // next-> MENU_BACK
};

const menuitem_t MENU_CAL_MENU[] = {
  {MT_SUBMENU, 0, "CAL WIZARD", MENU_CAL_WIZARD},
  {MT_SUBMENU, 0, "CAL OPTIONS", MENU_CAL_OPTIONS},
  {MT_SUBMENU, 0, "CAL MANAGE", MENU_CAL_MANAGEMENT},
  {MT_CALLBACK, 0, "SAVE CAL", menu_save_submenu_cb},
  {MT_CALLBACK, 0, "RECALL CAL", menu_recall_submenu_cb},
  {MT_NEXT, 0, NULL, MENU_BACK} // next-> MENU_BACK
};
