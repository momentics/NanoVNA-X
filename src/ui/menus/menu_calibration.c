#include "nanovna.h"
#include "hal.h"
#include "ui/menus/menu_internal.h"
#include "ui/input/hardware_input.h"
#include "chprintf.h"

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
  // This avoids potential memory allocation issues with menu_build_save_menu()
  menu_move_back(false);
  // This allows the user to save calibration separately via CAL -> SAVE CAL
  // which is safer and avoids potential memory allocation issues
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
    plot_printf(b->label, sizeof(b->label), "CAL: %dp\n %.6F" S_Hz "\n %.6F" S_Hz, cal_sweep_points,
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

static const menuitem_t menu_calop[] = {
    {MT_ADV_CALLBACK, CAL_OPEN, "OPEN", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_SHORT, "SHORT", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_LOAD, "LOAD", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_ISOLN, "ISOLN", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_THRU, "THRU", menu_calop_acb},
    //{ MT_ADV_CALLBACK, KM_EDELAY, "E-DELAY", menu_keyboard_acb },
    {MT_CALLBACK, 0, "DONE", menu_caldone_cb},
    {MT_CALLBACK, 1, "DONE IN RAM", menu_caldone_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_cal_wizard[] = {
    {MT_ADV_CALLBACK, CAL_OPEN, "OPEN", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_SHORT, "SHORT", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_LOAD, "LOAD", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_ISOLN, "ISOLN", menu_calop_acb},
    {MT_ADV_CALLBACK, CAL_THRU, "THRU", menu_calop_acb},
    {MT_CALLBACK, 0, "DONE", menu_caldone_cb},
    {MT_CALLBACK, 1, "DONE IN RAM", menu_caldone_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_cal_options[] = {
    {MT_ADV_CALLBACK, 0, "CAL RANGE", menu_cal_range_acb},
    {MT_ADV_CALLBACK, 0, "CAL POWER", menu_power_sel_acb},
    {MT_ADV_CALLBACK, 0, "ENHANCED\nRESPONSE", menu_cal_enh_acb},
#ifdef __VNA_Z_RENORMALIZATION__
    {MT_ADV_CALLBACK, KM_CAL_LOAD_R, "LOAD STD\n " R_LINK_COLOR "%bF" S_OHM, menu_keyboard_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_cal_management[] = {
    {MT_ADV_CALLBACK, 0, "CAL APPLY", menu_cal_apply_acb},
    {MT_CALLBACK, 0, "CAL RESET", menu_cal_reset_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_cal_menu[] = {
    {MT_SUBMENU, 0, "CAL WIZARD", menu_cal_wizard},
    {MT_SUBMENU, 0, "CAL OPTIONS", menu_cal_options},
    {MT_SUBMENU, 0, "CAL MANAGE", menu_cal_management},
    {MT_CALLBACK, 0, "SAVE CAL", menu_save_submenu_cb},
    {MT_CALLBACK, 0, "RECALL CAL", menu_recall_submenu_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
