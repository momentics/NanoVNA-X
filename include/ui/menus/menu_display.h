#ifndef __UI_MENU_DISPLAY_H__
#define __UI_MENU_DISPLAY_H__

#include "ui/ui_menu.h"

extern const menuitem_t menu_display[];
extern const menuitem_t menu_measure_tools[];
void input_amplitude(uint16_t data, button_t* b);
void input_scale(uint16_t data, button_t* b);
void input_ref(uint16_t data, button_t* b);
void input_edelay(uint16_t data, button_t* b);
void input_s21_offset(uint16_t data, button_t* b);
void input_velocity(uint16_t data, button_t* b);
void input_cable_len(uint16_t data, button_t* b);
void input_measure_r(uint16_t data, button_t* b);
void input_portz(uint16_t data, button_t* b);
 // Exposed for menu_main.c

#endif // __UI_MENU_DISPLAY_H__
