#pragma once

#include "ui/ui_menu.h"

// Core Menu Engine API
void menu_stack_reset(void);
void menu_draw(uint32_t mask);
void menu_invoke(int item);

// Handlers for menu interaction
void ui_menu_lever(uint16_t status);
void ui_menu_touch(int touch_x, int touch_y);

// Helpers
void menu_move_back(bool leave_ui);
void menu_push_submenu(const menuitem_t* submenu);
void menu_set_submenu(const menuitem_t* submenu);

// Exposed for ui_controller.c to build menus
const menuitem_t* current_menu_item(int i);
int current_menu_get_count(void);
void ui_mode_menu(void);

extern const menuitem_t menu_back[];

// Generic Menu Callbacks
UI_FUNCTION_ADV_CALLBACK(menu_keyboard_acb);

// Dynamic Menu Buffer (for dynamic construction)
menuitem_t* menu_dynamic_acquire(void);

// Exposed globals (from engine)
extern int8_t selection;
extern uint8_t menu_current_level;
