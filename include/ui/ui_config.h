/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 * Based on Dmitry (DiSlord) dislordlive@gmail.com and 
 * TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 */
#pragma once

#include <stdint.h>

// LCD display size settings
#ifdef LCD_320X240 // 320x240 display plot definitions
#define LCD_WIDTH 320
#define LCD_HEIGHT 240

// Define maximum distance in pixel for pickup marker (can be bigger for big displays)
#define MARKER_PICKUP_DISTANCE 20
// Used marker image settings
#define _USE_MARKER_SET_ 1
// Used font settings
#define USE_FONT_ID 1
#define USE_SMALL_FONT_ID 0

// Plot area size settings
// Offset of plot area (size of additional info at left side)
#define OFFSETX 10
#define OFFSETY 0

// Grid count, must divide
// #define NGRIDY                     10
#define NGRIDY 8

// Plot area WIDTH better be n*(POINTS_COUNT-1)
#define WIDTH 300
// Plot area HEIGHT = NGRIDY * GRIDY
#define HEIGHT 232

// GRIDX calculated depends from frequency span
// GRIDY depend from HEIGHT and NGRIDY, must be integer
#define GRIDY (HEIGHT / NGRIDY)

// Need for reference marker draw
#define CELLOFFSETX 5
#define AREA_WIDTH_NORMAL (CELLOFFSETX + WIDTH + 1 + 4)
#define AREA_HEIGHT_NORMAL (HEIGHT + 1)

// Smith/polar chart
#define P_CENTER_X (CELLOFFSETX + WIDTH / 2)
#define P_CENTER_Y (HEIGHT / 2)
#define P_RADIUS (HEIGHT / 2)

// Other settings (battery/calibration/frequency text position)
// Battery icon position
#define BATTERY_ICON_POSX 1
#define BATTERY_ICON_POSY 1

// Calibration text coordinates
#define CALIBRATION_INFO_POSX 0
#define CALIBRATION_INFO_POSY 100

#define FREQUENCIES_XPOS1 OFFSETX
#define FREQUENCIES_XPOS2 (LCD_WIDTH - SFONT_STR_WIDTH(23))
#define FREQUENCIES_XPOS3 (LCD_WIDTH / 2 + OFFSETX - SFONT_STR_WIDTH(16) / 2)
#define FREQUENCIES_YPOS (AREA_HEIGHT_NORMAL)
#endif // end 320x240 display plot definitions

#ifdef LCD_480X320 // 480x320 display definitions
#define LCD_WIDTH 480
#define LCD_HEIGHT 320

// Define maximum distance in pixel for pickup marker (can be bigger for big displays)
#define MARKER_PICKUP_DISTANCE 30
// Used marker image settings
#define _USE_MARKER_SET_ 2
// Used font settings
#define USE_FONT_ID 2
#define USE_SMALL_FONT_ID 2

// Plot area size settings
// Offset of plot area (size of additional info at left side)
#define OFFSETX 15
#define OFFSETY 0

// Grid count, must divide
// #define NGRIDY                     10
#define NGRIDY 8

// Plot area WIDTH better be n*(POINTS_COUNT-1)
#define WIDTH 455
// Plot area HEIGHT = NGRIDY * GRIDY
#define HEIGHT 304

// GRIDX calculated depends from frequency span
// GRIDY depend from HEIGHT and NGRIDY, must be integer
#define GRIDY (HEIGHT / NGRIDY)

// Need for reference marker draw
#define CELLOFFSETX 5
#define AREA_WIDTH_NORMAL (CELLOFFSETX + WIDTH + 1 + 4)
#define AREA_HEIGHT_NORMAL (HEIGHT + 1)

// Smith/polar chart
#define P_CENTER_X (CELLOFFSETX + WIDTH / 2)
#define P_CENTER_Y (HEIGHT / 2)
#define P_RADIUS (HEIGHT / 2)

// Other settings (battery/calibration/frequency text position)
// Battery icon position
#define BATTERY_ICON_POSX 3
#define BATTERY_ICON_POSY 2

// Calibration text coordinates
#define CALIBRATION_INFO_POSX 0
#define CALIBRATION_INFO_POSY 100

#define FREQUENCIES_XPOS1 OFFSETX
#define FREQUENCIES_XPOS2 (LCD_WIDTH - SFONT_STR_WIDTH(22))
#define FREQUENCIES_XPOS3 (LCD_WIDTH / 2 + OFFSETX - SFONT_STR_WIDTH(14) / 2)
#define FREQUENCIES_YPOS (AREA_HEIGHT_NORMAL + 2)
#endif // end 480x320 display plot definitions

// UI size defines
// Text offset in menu
#define MENU_TEXT_OFFSET 6
#define MENU_ICON_OFFSET 4
// Scale / ref quick touch position
#define UI_SCALE_REF_X0 (OFFSETX - 5)
#define UI_SCALE_REF_X1 (OFFSETX + CELLOFFSETX + 10)
// Leveler Marker mode select
#define UI_MARKER_Y0 30
// Maximum menu buttons count
#define MENU_BUTTON_MAX 16
#define MENU_BUTTON_MIN 8
// Menu buttons y offset
#define MENU_BUTTON_Y_OFFSET 1
// Menu buttons size = 21 for icon and 10 chars
#define MENU_BUTTON_WIDTH (7 + FONT_STR_WIDTH(12))
#define MENU_BUTTON_HEIGHT(n) (AREA_HEIGHT_NORMAL / (n))
#define MENU_BUTTON_BORDER 1
#define KEYBOARD_BUTTON_BORDER 1
#define BROWSER_BUTTON_BORDER 1
// Browser window settings
#define FILES_COLUMNS (LCD_WIDTH / 160)             // columns in browser
#define FILES_ROWS 10                               // rows in browser
#define FILES_PER_PAGE (FILES_COLUMNS * FILES_ROWS) // FILES_ROWS * FILES_COLUMNS
#define FILE_BOTTOM_HEIGHT 20                       // Height of bottom buttons (< > X)
#define FILE_BUTTON_HEIGHT                                                                         \
  ((LCD_HEIGHT - FILE_BOTTOM_HEIGHT) / FILES_ROWS) // Height of file buttons

// Define message box width
#define MESSAGE_BOX_WIDTH 180

// Height of numerical input field (at bottom)
#define NUM_INPUT_HEIGHT 32
// On screen keyboard button size
#if 1                                                   // Full screen keyboard
#define KP_WIDTH (LCD_WIDTH / 4)                        // numeric keypad button width
#define KP_HEIGHT ((LCD_HEIGHT - NUM_INPUT_HEIGHT) / 4) // numeric keypad button height
#define KP_X_OFFSET 0                                   // numeric keypad X offset
#define KP_Y_OFFSET 0                                   // numeric keypad Y offset
#define KPF_WIDTH (LCD_WIDTH / 10)                      // text keypad button width
#define KPF_HEIGHT KPF_WIDTH                            // text keypad button height
#define KPF_X_OFFSET 0                                  // text keypad X offset
#define KPF_Y_OFFSET (LCD_HEIGHT - NUM_INPUT_HEIGHT - 4 * KPF_HEIGHT) // text keypad Y offset
#else                                                                 // 64 pixel size keyboard
#define KP_WIDTH 64                                                   // numeric keypad button width
#define KP_HEIGHT 64 // numeric keypad button height
#define KP_X_OFFSET (LCD_WIDTH - MENU_BUTTON_WIDTH - 16 - KP_WIDTH * 4) // numeric keypad X offset
#define KP_Y_OFFSET 20                                                  // numeric keypad Y offset
#define KPF_WIDTH (LCD_WIDTH / 10)                                      // text keypad button width
#define KPF_HEIGHT KPF_WIDTH                                            // text keypad button height
#define KPF_X_OFFSET 0                                                  // text keypad X offset
#define KPF_Y_OFFSET (LCD_HEIGHT - NUM_INPUT_HEIGHT - 4 * KPF_HEIGHT)   // text keypad Y offset
#endif

#if USE_FONT_ID == 0
extern const uint8_t X5X7_BITS[];
#define FONT_START_CHAR 0x16
#define FONT_WIDTH 5
#define FONT_GET_HEIGHT 7
#define FONT_STR_WIDTH(n) ((n) * FONT_WIDTH)
#define FONT_STR_HEIGHT 8
#define FONT_GET_DATA(ch) (&X5X7_BITS[((ch) - FONT_START_CHAR) * FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch) (8 - (X5X7_BITS[((ch) - FONT_START_CHAR) * FONT_GET_HEIGHT] & 0x7))

#elif USE_FONT_ID == 1
extern const uint8_t X6X10_BITS[];
#define FONT_START_CHAR 0x16
#define FONT_WIDTH 6
#define FONT_GET_HEIGHT 10
#define FONT_STR_WIDTH(n) ((n) * FONT_WIDTH)
#define FONT_STR_HEIGHT 11
#define FONT_GET_DATA(ch) (&X6X10_BITS[(ch - FONT_START_CHAR) * FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch) (8 - (X6X10_BITS[(ch - FONT_START_CHAR) * FONT_GET_HEIGHT] & 0x7))

#elif USE_FONT_ID == 2
extern const uint8_t X7X11B_BITS[];
#define FONT_START_CHAR 0x16
#define FONT_WIDTH 7
#define FONT_GET_HEIGHT 11
#define FONT_STR_WIDTH(n) ((n) * FONT_WIDTH)
#define FONT_STR_HEIGHT 11
#define FONT_GET_DATA(ch) (&X7X11B_BITS[(ch - FONT_START_CHAR) * FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch) (8 - (X7X11B_BITS[(ch - FONT_START_CHAR) * FONT_GET_HEIGHT] & 7))

#elif USE_FONT_ID == 3
extern const uint8_t X11X14_BITS[];
#define FONT_START_CHAR 0x16
#define FONT_WIDTH 11
#define FONT_GET_HEIGHT 14
#define FONT_STR_WIDTH(n) ((n) * FONT_WIDTH)
#define FONT_STR_HEIGHT 16
#define FONT_GET_DATA(ch) (&X11X14_BITS[(ch - FONT_START_CHAR) * 2 * FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch)                                                                         \
  (14 - (X11X14_BITS[(ch - FONT_START_CHAR) * 2 * FONT_GET_HEIGHT + 1] & 0x7))
#endif

#if USE_SMALL_FONT_ID == 0
extern const uint8_t X5X7_BITS[];
#define SFONT_START_CHAR 0x16
#define SFONT_WIDTH 5
#define SFONT_GET_HEIGHT 7
#define SFONT_STR_WIDTH(n) ((n) * SFONT_WIDTH)
#define SFONT_STR_HEIGHT 8
#define SFONT_GET_DATA(ch) (&X5X7_BITS[((ch) - SFONT_START_CHAR) * SFONT_GET_HEIGHT])
#define SFONT_GET_WIDTH(ch) (8 - (X5X7_BITS[((ch) - SFONT_START_CHAR) * SFONT_GET_HEIGHT] & 0x7))

#elif USE_SMALL_FONT_ID == 1
extern const uint8_t X6X10_BITS[];
#define SFONT_START_CHAR 0x16
#define SFONT_WIDTH 6
#define SFONT_GET_HEIGHT 10
#define SFONT_STR_WIDTH(n) ((n) * SFONT_WIDTH)
#define SFONT_STR_HEIGHT 11
#define SFONT_GET_DATA(ch) (&X6X10_BITS[(ch - SFONT_START_CHAR) * SFONT_GET_HEIGHT])
#define SFONT_GET_WIDTH(ch) (8 - (X6X10_BITS[(ch - SFONT_START_CHAR) * SFONT_GET_HEIGHT] & 0x7))

#elif USE_SMALL_FONT_ID == 2
extern const uint8_t X7X11B_BITS[];
#define SFONT_START_CHAR 0x16
#define SFONT_WIDTH 7
#define SFONT_GET_HEIGHT 11
#define SFONT_STR_WIDTH(n) ((n) * SFONT_WIDTH)
#define SFONT_STR_HEIGHT 11
#define SFONT_GET_DATA(ch) (&X7X11B_BITS[(ch - SFONT_START_CHAR) * SFONT_GET_HEIGHT])
#define SFONT_GET_WIDTH(ch) (8 - (X7X11B_BITS[(ch - SFONT_START_CHAR) * SFONT_GET_HEIGHT] & 7))

#elif USE_SMALL_FONT_ID == 3
extern const uint8_t X11X14_BITS[];
#define SFONT_START_CHAR 0x16
#define SFONT_WIDTH 11
#define SFONT_GET_HEIGHT 14
#define SFONT_STR_WIDTH(n) ((n) * SFONT_WIDTH)
#define SFONT_STR_HEIGHT 16
#define SFONT_GET_DATA(ch) (&X11X14_BITS[(ch - SFONT_START_CHAR) * 2 * SFONT_GET_HEIGHT])
#define SFONT_GET_WIDTH(ch)                                                                        \
  (14 - (X11X14_BITS[(ch - SFONT_START_CHAR) * 2 * SFONT_GET_HEIGHT + 1] & 0x7))
#endif

// Font type defines
enum { FONT_SMALL = 0, FONT_NORMAL };
#if USE_FONT_ID != USE_SMALL_FONT_ID
void lcd_set_font(int type);
#define LCD_SET_FONT(type) lcd_set_font(type)
#else
#define LCD_SET_FONT(type) (void)(type)
#endif

extern const uint8_t NUMFONT16X22[];
#define NUM_FONT_GET_WIDTH 16
#define NUM_FONT_GET_HEIGHT 22
#define NUM_FONT_GET_DATA(ch) (&NUMFONT16X22[(ch) * 2 * NUM_FONT_GET_HEIGHT])

// Glyph names from NUMFONT16X22.c
enum {
  KP_0 = 0,
  KP_1,
  KP_2,
  KP_3,
  KP_4,
  KP_5,
  KP_6,
  KP_7,
  KP_8,
  KP_9,
  KP_PERIOD,
  KP_MINUS,
  KP_BS,
  KP_k,
  KP_M,
  KP_G,
  KP_m,
  KP_u,
  KP_n,
  KP_p,
  KP_X1,
  KP_ENTER,
  KP_PERCENT, // Enter values
#if 0
  KP_INF,
  KP_DB,
  KP_PLUSMINUS,
  KP_KEYPAD,
  KP_SPACE,
  KP_PLUS,
#endif
  // Special uint8_t buttons
  KP_EMPTY = 255 // Empty button
};
