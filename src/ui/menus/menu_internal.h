#pragma once

#include "nanovna.h"
#include "ui/ui_internal.h"

// Navigation support
#define markers current_props._markers
#define active_marker current_props._active_marker
#define previous_marker current_props._previous_marker
#define trace current_props._trace
#define current_trace current_props._current_trace

void menu_move_back(bool leave_ui);
void menu_push_submenu(const menuitem_t* submenu);
void menu_set_submenu(const menuitem_t* submenu);
menuitem_t* ui_menu_list(const menu_descriptor_t* descriptors, size_t count,
                         const char* label, const void* reference,
                         menuitem_t* out);
void menu_set_next(menuitem_t* entry, const menuitem_t* next);
extern const menuitem_t menu_back[];

// Shared Callbacks from ui_controller.c
void menu_save_submenu_cb(uint16_t data);
void menu_recall_submenu_cb(uint16_t data);
void menu_power_sel_acb(uint16_t data, button_t* b);
UI_FUNCTION_CALLBACK(menu_config_cb);
UI_FUNCTION_ADV_CALLBACK(menu_band_sel_acb);
UI_FUNCTION_ADV_CALLBACK(menu_bandwidth_sel_acb);
UI_FUNCTION_ADV_CALLBACK(menu_points_sel_acb);
UI_FUNCTION_ADV_CALLBACK(menu_keyboard_acb);
UI_FUNCTION_CALLBACK(menu_measure_cb);
UI_FUNCTION_ADV_CALLBACK(menu_measure_acb);

const menuitem_t* menu_build_bandwidth_menu(void);
const menuitem_t* menu_build_points_menu(void);
const menuitem_t* menu_build_save_menu(void);
const menuitem_t* menu_build_recall_menu(void);
const menuitem_t* menu_build_smooth_menu(void);
const menuitem_t* menu_build_power_menu(void);
const menuitem_t* menu_build_serial_speed_menu(void);
// menu_cal_options used menu_power_sel_acb.

// Calibration Menu (menu_calibration.c)
extern const menuitem_t menu_cal_menu[];

// Display Menu (menu_display.c)
extern const menuitem_t menu_display[];
extern const menuitem_t menu_marker[];
extern const menuitem_t menu_stimulus[];
const menuitem_t* menu_build_marker_select_menu(void);

// Smith helper from ui_controller.c
const menuitem_t* menu_build_marker_smith_menu(uint8_t channel);
uint8_t get_smith_format(void);
void menu_auto_scale_cb(uint16_t data);
UI_FUNCTION_ADV_CALLBACK(menu_marker_smith_acb);
UI_FUNCTION_ADV_CALLBACK(menu_vna_mode_acb);

// Dynamic Menu Support
menuitem_t* menu_dynamic_acquire(void);

#define UI_MARKER_EDELAY 6
