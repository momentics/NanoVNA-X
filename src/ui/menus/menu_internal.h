#pragma once

#include "nanovna.h"
#include "ui/ui_internal.h"

// Navigation support
void menu_move_back(bool leave_ui);
void menu_push_submenu(const menuitem_t* submenu);
extern const menuitem_t menu_back[];

// Shared Callbacks from ui_controller.c
void menu_save_submenu_cb(uint16_t data);
void menu_recall_submenu_cb(uint16_t data);
void menu_power_sel_acb(uint16_t data, button_t* b);
UI_FUNCTION_CALLBACK(menu_config_cb); // Check if needed? menu_system uses it.
// menu_cal_options used menu_power_sel_acb.

// Calibration Menu (menu_calibration.c)
extern const menuitem_t menu_cal_menu[];

// Display Menu (menu_display.c)
extern const menuitem_t menu_display[];
extern const menuitem_t menu_marker[];

// Smith helper from ui_controller.c
const menuitem_t* menu_build_marker_smith_menu(uint8_t channel);
uint8_t get_smith_format(void);
void menu_auto_scale_cb(uint16_t data);
