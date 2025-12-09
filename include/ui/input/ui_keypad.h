#pragma once

#include "nanovna.h"
#include "ui/ui_internal.h"

extern uint8_t keypad_mode;

void ui_mode_keypad(int mode);
void ui_keypad_touch(int touch_x, int touch_y);
void ui_keypad_lever(uint16_t status);
void ui_keyboard_cb(uint16_t data, button_t* b);

float keyboard_get_float(void);
freq_t keyboard_get_freq(void);
uint32_t keyboard_get_uint(void);
int32_t keyboard_get_int(void);
