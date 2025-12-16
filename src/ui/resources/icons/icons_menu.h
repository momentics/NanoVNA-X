/*
 * UI icon resource declarations.
 */

#pragma once

#include <stdint.h>

#include "nanovna.h"

#if LCD_WIDTH < 800
#define CALIBRATION_OFFSET 16
#define TOUCH_MARK_W 9
#define TOUCH_MARK_H 9
#define TOUCH_MARK_X 4
#define TOUCH_MARK_Y 4
#else
#define CALIBRATION_OFFSET 16
#define TOUCH_MARK_W 15
#define TOUCH_MARK_H 15
#define TOUCH_MARK_X 7
#define TOUCH_MARK_Y 7
#endif

extern const uint8_t touch_bitmap[];

#if _USE_FONT_ < 3
#define ICON_SIZE 14
#define ICON_WIDTH 11
#define ICON_HEIGHT 11
#else
#define ICON_SIZE 18
#define ICON_WIDTH 14
#define ICON_HEIGHT 14
#endif

extern const uint8_t button_icons[];
#define ICON_GET_DATA(i) (&button_icons[2 * ICON_HEIGHT * (i)])

