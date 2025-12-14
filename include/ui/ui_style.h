#pragma once

#include <stdint.h>
#include "core/config_macros.h"

// Set display buffers count for cell render (if use 2 and DMA, possible send data and prepare new
// in some time)
#ifdef USE_DISPLAY_DMA
// Cell size = sizeof(spi_buffer), but need wait while cell data send to LCD
// #define DISPLAY_CELL_BUFFER_COUNT     1
// Cell size = sizeof(spi_buffer)/2, while one cell send to LCD by DMA, CPU render to next cell
#define DISPLAY_CELL_BUFFER_COUNT 2
#else
// Always one if no DMA mode
#define DISPLAY_CELL_BUFFER_COUNT 1
#endif

// Custom display driver panel definitions for ILI9341
#if defined(LCD_DRIVER_ILI9341) || defined(LCD_DRIVER_ST7789)
// LCD touch settings
#define DEFAULT_TOUCH_CONFIG                                                                       \
  { 530, 795, 3460, 3350 } // 2.8 inch LCD panel
// Define LCD pixel format (8 or 16 bit)
// #define LCD_8BIT_MODE
#define LCD_16BIT_MODE
// Default LCD brightness if display support it
#define DEFAULT_BRIGHTNESS 80
// Data size for one pixel data read from display in bytes
#define LCD_RX_PIXEL_SIZE 3
#endif

// Custom display driver panel definitions for ST7796S
#ifdef LCD_DRIVER_ST7796S
// LCD touch settings
#define DEFAULT_TOUCH_CONFIG                                                                       \
  { 380, 665, 3600, 3450 } // 4.0 inch LCD panel
// Define LCD pixel format (8 or 16 bit)
// #define LCD_8BIT_MODE
#define LCD_16BIT_MODE
// Default LCD brightness if display support it
#define DEFAULT_BRIGHTNESS 80
// Data size for one pixel data read from display in bytes
#define LCD_RX_PIXEL_SIZE 2
#endif

// For 8 bit color displays pixel data definitions
#ifdef LCD_8BIT_MODE
typedef uint8_t pixel_t;
//  8-bit RRRGGGBB
// #define RGB332(r,g,b)  ( (((r)&0xE0)>>0) | (((g)&0xE0)>>3) | (((b)&0xC0)>>5))
#define RGB565(r, g, b) ((((r) & 0xE0) >> 0) | (((g) & 0xE0) >> 3) | (((b) & 0xC0) >> 6))
#define RGBHEX(hex)                                                                                \
  ((((hex) & 0xE00000) >> 16) | (((hex) & 0x00E000) >> 11) | (((hex) & 0x0000C0) >> 6))
#define HEXRGB(hex)                                                                                \
  ((((hex) << 16) & 0xE00000) | (((hex) << 11) & 0x00E000) | (((hex) << 6) & 0x0000C0))
#define LCD_PIXEL_SIZE 1
// Cell size, depends from spi_buffer size, CELLWIDTH*CELLHEIGHT*sizeof(pixel) <= sizeof(spi_buffer)
#define CELLWIDTH (64 / DISPLAY_CELL_BUFFER_COUNT)
#define CELLHEIGHT (64)
#endif

// For 16 bit color displays pixel data definitions
#ifdef LCD_16BIT_MODE
typedef uint16_t pixel_t;
// SPI bus revert byte order
// 16-bit gggBBBbb RRRrrGGG
#define RGB565(r, g, b)                                                                            \
  ((((g) & 0x1c) << 11) | (((b) & 0xf8) << 5) | ((r) & 0xf8) | (((g) & 0xe0) >> 5))
#define RGBHEX(hex)                                                                                \
  ((((hex) & 0x001c00) << 3) | (((hex) & 0x0000f8) << 5) | (((hex) & 0xf80000) >> 16) |            \
   (((hex) & 0x00e000) >> 13))
#define HEXRGB(hex)                                                                                \
  ((((hex) >> 3) & 0x001c00) | (((hex) >> 5) & 0x0000f8) | (((hex) << 16) & 0xf80000) |            \
   (((hex) << 13) & 0x00e000))
#define LCD_PIXEL_SIZE 2
// Cell size, depends from spi_buffer size, CELLWIDTH*CELLHEIGHT*sizeof(pixel) <= sizeof(spi_buffer)
#define CELLWIDTH (64 / DISPLAY_CELL_BUFFER_COUNT)
#define CELLHEIGHT (16)
#endif

// Define size of screen buffer in pixel_t (need for cell w * h * count)
#define SPI_BUFFER_SIZE (CELLWIDTH * CELLHEIGHT * DISPLAY_CELL_BUFFER_COUNT)

#ifndef SPI_BUFFER_SIZE
#error "Define LCD pixel format"
#endif

enum {
  LCD_BG_COLOR = 0,      // background
  LCD_FG_COLOR,          // foreground (in most cases text on background)
  LCD_GRID_COLOR,        // grid lines color
  LCD_MENU_COLOR,        // UI menu color
  LCD_MENU_TEXT_COLOR,   // UI menu text color
  LCD_MENU_ACTIVE_COLOR, // UI selected menu color
  LCD_TRACE_1_COLOR,     // Trace 1 color
  LCD_TRACE_2_COLOR,     // Trace 2 color
  LCD_TRACE_3_COLOR,     // Trace 3 color
  LCD_TRACE_4_COLOR,     // Trace 4 color
  LCD_TRACE_5_COLOR,     // Stored trace A color
  LCD_TRACE_6_COLOR,     // Stored trace B color
  LCD_NORMAL_BAT_COLOR,  // Normal battery icon color
  LCD_LOW_BAT_COLOR,     // Low battery icon color
  LCD_SPEC_INPUT_COLOR,  // Not used, for future
  LCD_RISE_EDGE_COLOR,   // UI menu button rise edge color
  LCD_FALLEN_EDGE_COLOR, // UI menu button fallen edge color
  LCD_SWEEP_LINE_COLOR,  // Sweep line color
  LCD_BW_TEXT_COLOR,     // Bandwidth text color
  LCD_INPUT_TEXT_COLOR,  // Keyboard Input text color
  LCD_INPUT_BG_COLOR,    // Keyboard Input text background color
  LCD_MEASURE_COLOR,     // Measure text color
  LCD_GRID_VALUE_COLOR,  // Not used, for future
  LCD_INTERP_CAL_COLOR,  // Calibration state on interpolation color
  LCD_DISABLE_CAL_COLOR, // Calibration state on disable color
  LCD_LINK_COLOR,        // UI menu button text for values color
  LCD_TXT_SHADOW_COLOR,  // Plot area text border color
};

#define LCD_DEFAULT_PALETTE                                                                        \
  {                                                                                                \
    [LCD_BG_COLOR] = RGB565(0, 0, 0), [LCD_FG_COLOR] = RGB565(255, 255, 255),                      \
    [LCD_GRID_COLOR] = RGB565(128, 128, 128), [LCD_MENU_COLOR] = RGB565(230, 230, 230),            \
    [LCD_MENU_TEXT_COLOR] = RGB565(0, 0, 0), [LCD_MENU_ACTIVE_COLOR] = RGB565(210, 210, 210),      \
    [LCD_TRACE_1_COLOR] = RGB565(255, 255, 0), [LCD_TRACE_2_COLOR] = RGB565(0, 255, 255),          \
    [LCD_TRACE_3_COLOR] = RGB565(0, 255, 0), [LCD_TRACE_4_COLOR] = RGB565(255, 0, 255),            \
    [LCD_TRACE_5_COLOR] = RGB565(255, 0, 0), [LCD_TRACE_6_COLOR] = RGB565(0, 0, 255),              \
    [LCD_NORMAL_BAT_COLOR] = RGB565(31, 227, 0), [LCD_LOW_BAT_COLOR] = RGB565(255, 0, 0),          \
    [LCD_SPEC_INPUT_COLOR] = RGB565(128, 255, 128), [LCD_RISE_EDGE_COLOR] = RGB565(255, 255, 255), \
    [LCD_FALLEN_EDGE_COLOR] = RGB565(128, 128, 128), [LCD_SWEEP_LINE_COLOR] = RGB565(0, 0, 255),   \
    [LCD_BW_TEXT_COLOR] = RGB565(196, 196, 196), [LCD_INPUT_TEXT_COLOR] = RGB565(0, 0, 0),         \
    [LCD_INPUT_BG_COLOR] = RGB565(255, 255, 255), [LCD_MEASURE_COLOR] = RGB565(255, 255, 255),     \
    [LCD_GRID_VALUE_COLOR] = RGB565(96, 96, 96), [LCD_INTERP_CAL_COLOR] = RGB565(31, 227, 0),      \
    [LCD_DISABLE_CAL_COLOR] = RGB565(255, 0, 0), [LCD_LINK_COLOR] = RGB565(0, 0, 192),             \
    [LCD_TXT_SHADOW_COLOR] = RGB565(0, 0, 0),                                                      \
  }

#define GET_PALTETTE_COLOR(idx) config._lcd_palette[idx]
