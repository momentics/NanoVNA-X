#pragma once

#include "ui/ui_menu.h"

void input_freq(uint16_t data, button_t *b);
void input_var_delay(uint16_t data, button_t *b);
void input_points(uint16_t data, button_t *b);

extern const menuitem_t MENU_STIMULUS[];
