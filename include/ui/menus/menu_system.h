#pragma once

#include "ui/ui_menu.h"

extern const menuitem_t MENU_SYSTEM[];
UI_FUNCTION_ADV_CALLBACK(menu_offset_sel_acb);
UI_FUNCTION_ADV_CALLBACK(menu_vna_mode_acb);

void menu_sys_info(void);
void lcd_set_brightness(uint16_t b);

void input_xtal(uint16_t data, button_t *b);
void input_harmonic(uint16_t data, button_t *b);
void input_vbat(uint16_t data, button_t *b);
void input_date_time(uint16_t data, button_t *b);
void input_rtc_cal(uint16_t data, button_t *b);
