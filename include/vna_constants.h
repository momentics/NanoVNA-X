#pragma once

#include "vna_config.h"
#include <stdint.h> 

// Speed of light const
#define SPEED_OF_LIGHT           299792458

// pi const
#define VNA_PI                   3.14159265358979323846f
#define VNA_TWOPI                6.28318530717958647692f

// Max palette indexes in config
#define MAX_PALETTE     32

// trace 
#define MAX_TRACE_TYPE 30

// Store traces count
#define STORED_TRACES  1
#define TRACES_MAX     4

// marker 1 to 8
#define MARKERS_MAX 8

#define MK_SEARCH_LEFT      -1
#define MK_SEARCH_RIGHT      1

#define CAL_TYPE_COUNT  5
#define CAL_LOAD        0
#define CAL_OPEN        1
#define CAL_SHORT       2
#define CAL_THRU        3
#define CAL_ISOLN       4

#define CALSTAT_LOAD (1<<0)
#define CALSTAT_OPEN (1<<1)
#define CALSTAT_SHORT (1<<2)
#define CALSTAT_THRU (1<<3)
#define CALSTAT_ISOLN (1<<4)
#define CALSTAT_ES (1<<5)
#define CALSTAT_ER (1<<6)
#define CALSTAT_ET (1<<7)
#define CALSTAT_ED CALSTAT_LOAD
#define CALSTAT_EX CALSTAT_ISOLN
#define CALSTAT_APPLY (1<<8)
#define CALSTAT_INTERPOLATED (1<<9)
#define CALSTAT_ENHANCED_RESPONSE (1<<10)

#define ETERM_ED 0 /* error term directivity */
#define ETERM_ES 1 /* error term source match */
#define ETERM_ER 2 /* error term refrection tracking */
#define ETERM_ET 3 /* error term transmission tracking */
#define ETERM_EX 4 /* error term isolation */

#if   SWEEP_POINTS_MAX <= 256
#define FFT_SIZE   256
#elif SWEEP_POINTS_MAX <= 512
#define FFT_SIZE   512
#endif

// Sweep point options
#define POINTS_COUNT_DEFAULT 101
#if SWEEP_POINTS_MAX > 201
#define POINTS_SET_COUNT 5
#define POINTS_SET      {51, 101, 201, 301, 401}
#else
#define POINTS_SET_COUNT 3
#define POINTS_SET      {51, 101, 201}
#endif

// Render control chars
#define R_BGCOLOR  "\x01"  // hex 0x01 set background color
#define R_FGCOLOR  "\x02"  // hex 0x02 set foreground color
// Set BG / FG color string macros
#define SET_BGCOLOR(idx)   R_BGCOLOR #idx
#define SET_FGCOLOR(idx)   R_FGCOLOR #idx

#define R_TEXT_COLOR       SET_FGCOLOR(\x01)  // set  1 color index as foreground
#define R_LINK_COLOR       SET_FGCOLOR(\x19)  // set 25 color index as foreground

// Additional chars in fonts
#define S_ENTER    "\x16"  // hex 0x16
#define S_DELTA    "\x17"  // hex 0x17
#define S_SARROW   "\x18"  // hex 0x18
#define S_INFINITY "\x19"  // hex 0x19
#define S_LARROW   "\x1A"  // hex 0x1A
#define S_RARROW   "\x1B"  // hex 0x1B
#define S_PI       "\x1C"  // hex 0x1C
#define S_MICRO    "\x1D"  // hex 0x1D
#define S_OHM      "\x1E"  // hex 0x1E
#define S_DEGREE   "\x1F"  // hex 0x1F
#define S_SIEMENS  "S"     //
#define S_dB       "dB"    //
#define S_Hz       "Hz"    //
#define S_FARAD    "F"     //
#define S_HENRY    "H"     //
#define S_SECOND   "s"     //
#define S_METRE    "m"     //
#define S_VOLT     "V"     //
#define S_AMPER    "A"     //
#define S_PPM      "ppm"   //


/*
 * LCD Logic
 */
// Set display buffers count for cell render (if use 2 and DMA, possible send data and prepare new in some time)
#ifdef __USE_DISPLAY_DMA__
// Cell size = sizeof(spi_buffer), but need wait while cell data send to LCD
//#define DISPLAY_CELL_BUFFER_COUNT     1
// Cell size = sizeof(spi_buffer)/2, while one cell send to LCD by DMA, CPU render to next cell
#define DISPLAY_CELL_BUFFER_COUNT     2
#else
// Always one if no DMA mode
#define DISPLAY_CELL_BUFFER_COUNT     1
#endif

// Custom display driver panel definitions for ILI9341
#if defined(LCD_DRIVER_ILI9341) || defined(LCD_DRIVER_ST7789)
// LCD touch settings
#define DEFAULT_TOUCH_CONFIG {530, 795, 3460, 3350}    // 2.8 inch LCD panel
// Define LCD pixel format (8 or 16 bit)
//#define LCD_8BIT_MODE
#define LCD_16BIT_MODE
// Default LCD brightness if display support it
#define DEFAULT_BRIGHTNESS  80
// Data size for one pixel data read from display in bytes
#define LCD_RX_PIXEL_SIZE  3
#endif

// Custom display driver panel definitions for ST7796S
#ifdef LCD_DRIVER_ST7796S
// LCD touch settings
#define DEFAULT_TOUCH_CONFIG {380, 665, 3600, 3450 }  // 4.0 inch LCD panel
// Define LCD pixel format (8 or 16 bit)
//#define LCD_8BIT_MODE
#define LCD_16BIT_MODE
// Default LCD brightness if display support it
#define DEFAULT_BRIGHTNESS  80
// Data size for one pixel data read from display in bytes
#define LCD_RX_PIXEL_SIZE  2
#endif

#define TOUCH_THRESHOLD 2000

// For 8 bit color displays pixel data definitions
#ifdef LCD_8BIT_MODE
//  8-bit RRRGGGBB
//#define RGB332(r,g,b)  ( (((r)&0xE0)>>0) | (((g)&0xE0)>>3) | (((b)&0xC0)>>5))
#define RGB565(r,g,b)  ( (((r)&0xE0)>>0) | (((g)&0xE0)>>3) | (((b)&0xC0)>>6))
#define RGBHEX(hex)    ( (((hex)&0xE00000)>>16) | (((hex)&0x00E000)>>11) | (((hex)&0x0000C0)>>6) )
#define HEXRGB(hex)    ( (((hex)<<16)&0xE00000) | (((hex)<<11)&0x00E000) | (((hex)<<6)&0x0000C0) )
#define LCD_PIXEL_SIZE        1
// Cell size, depends from spi_buffer size, CELLWIDTH*CELLHEIGHT*sizeof(pixel) <= sizeof(spi_buffer)
#define CELLWIDTH  (64/DISPLAY_CELL_BUFFER_COUNT)
#define CELLHEIGHT (64)
#endif

// For 16 bit color displays pixel data definitions
#ifdef LCD_16BIT_MODE
// SPI bus revert byte order
// 16-bit gggBBBbb RRRrrGGG
#define RGB565(r,g,b)  ( (((g)&0x1c)<<11) | (((b)&0xf8)<<5) | ((r)&0xf8) | (((g)&0xe0)>>5) )
#define RGBHEX(hex) ( (((hex)&0x001c00)<<3) | (((hex)&0x0000f8)<<5) | (((hex)&0xf80000)>>16) | (((hex)&0x00e000)>>13) )
#define HEXRGB(hex) ( (((hex)>>3)&0x001c00) | (((hex)>>5)&0x0000f8) | (((hex)<<16)&0xf80000) | (((hex)<<13)&0x00e000) )
#define LCD_PIXEL_SIZE        2
// Cell size, depends from spi_buffer size, CELLWIDTH*CELLHEIGHT*sizeof(pixel) <= sizeof(spi_buffer)
#define CELLWIDTH  (64 / DISPLAY_CELL_BUFFER_COUNT)
#define CELLHEIGHT (16)
#endif

// Define size of screen buffer in pixel_t (need for cell w * h * count)
#define SPI_BUFFER_SIZE             (CELLWIDTH * CELLHEIGHT * DISPLAY_CELL_BUFFER_COUNT)

#ifndef SPI_BUFFER_SIZE
#error "Define LCD pixel format"
#endif

// Used for easy define big Bitmap as 0bXXXXXXXXX image
#define _BMP8(d)                                                        ((d)&0xFF)
#define _BMP16(d)                                      (((d)>>8)&0xFF), ((d)&0xFF)
#define _BMP24(d)                    (((d)>>16)&0xFF), (((d)>>8)&0xFF), ((d)&0xFF)
#define _BMP32(d)  (((d)>>24)&0xFF), (((d)>>16)&0xFF), (((d)>>8)&0xFF), ((d)&0xFF)

/*
 * flasch.c constants
 */
#define CONFIG_MAGIC      0x434f4e56 // Config magic value (allow reset on new config version)
#define PROPERTIES_MAGIC  0x434f4e54 // Properties magic value (allow reset on new properties version)

#define NO_SAVE_SLOT      ((uint16_t)(-1))

/*
 * plot.c settings
 */
// LCD display size settings
#ifdef LCD_320x240 // 320x240 display plot definitions
#define LCD_WIDTH                   320
#define LCD_HEIGHT                  240

// Define maximum distance in pixel for pickup marker (can be bigger for big displays)
#define MARKER_PICKUP_DISTANCE       20
// Used marker image settings
#define _USE_MARKER_SET_              1
// Used font settings
#define _USE_FONT_                    1
#define _USE_SMALL_FONT_              0

// Plot area size settings
// Offset of plot area (size of additional info at left side)
#define OFFSETX                      10
#define OFFSETY                       0

// Grid count, must divide
//#define NGRIDY                     10
#define NGRIDY                        8

// Plot area WIDTH better be n*(POINTS_COUNT-1)
#define WIDTH                       300
// Plot area HEIGHT = NGRIDY * GRIDY
#define HEIGHT                      232

// GRIDX calculated depends from frequency span
// GRIDY depend from HEIGHT and NGRIDY, must be integer
#define GRIDY                       (HEIGHT / NGRIDY)

// Need for reference marker draw
#define CELLOFFSETX                   5
#define AREA_WIDTH_NORMAL           (CELLOFFSETX + WIDTH  + 1 + 4)
#define AREA_HEIGHT_NORMAL          (              HEIGHT + 1)

// Smith/polar chart
#define P_CENTER_X                  (CELLOFFSETX + WIDTH/2)
#define P_CENTER_Y                  (HEIGHT/2)
#define P_RADIUS                    (HEIGHT/2)

// Other settings (battery/calibration/frequency text position)
// Battery icon position
#define BATTERY_ICON_POSX             1
#define BATTERY_ICON_POSY             1

// Calibration text coordinates
#define CALIBRATION_INFO_POSX         0
#define CALIBRATION_INFO_POSY       100

#define FREQUENCIES_XPOS1           OFFSETX
#define FREQUENCIES_XPOS2           (LCD_WIDTH - sFONT_STR_WIDTH(23))
#define FREQUENCIES_XPOS3           (LCD_WIDTH/2 + OFFSETX - sFONT_STR_WIDTH(16) / 2)
#define FREQUENCIES_YPOS            (AREA_HEIGHT_NORMAL)
#endif // end 320x240 display plot definitions

#ifdef LCD_480x320 // 480x320 display definitions
#define LCD_WIDTH                   480
#define LCD_HEIGHT                  320

// Define maximum distance in pixel for pickup marker (can be bigger for big displays)
#define MARKER_PICKUP_DISTANCE       30
// Used marker image settings
#define _USE_MARKER_SET_              2
// Used font settings
#define _USE_FONT_                    2
#define _USE_SMALL_FONT_              2

// Plot area size settings
// Offset of plot area (size of additional info at left side)
#define OFFSETX                      15
#define OFFSETY                       0

// Grid count, must divide
//#define NGRIDY                     10
#define NGRIDY                        8

// Plot area WIDTH better be n*(POINTS_COUNT-1)
#define WIDTH                       455
// Plot area HEIGHT = NGRIDY * GRIDY
#define HEIGHT                      304

// GRIDX calculated depends from frequency span
// GRIDY depend from HEIGHT and NGRIDY, must be integer
#define GRIDY                       (HEIGHT / NGRIDY)

// Need for reference marker draw
#define CELLOFFSETX                   5
#define AREA_WIDTH_NORMAL           (CELLOFFSETX + WIDTH  + 1 + 4)
#define AREA_HEIGHT_NORMAL          (              HEIGHT + 1)

// Smith/polar chart
#define P_CENTER_X                  (CELLOFFSETX + WIDTH/2)
#define P_CENTER_Y                  (HEIGHT/2)
#define P_RADIUS                    (HEIGHT/2)

// Other settings (battery/calibration/frequency text position)
// Battery icon position
#define BATTERY_ICON_POSX             3
#define BATTERY_ICON_POSY             2

// Calibration text coordinates
#define CALIBRATION_INFO_POSX         0
#define CALIBRATION_INFO_POSY       100

#define FREQUENCIES_XPOS1           OFFSETX
#define FREQUENCIES_XPOS2           (LCD_WIDTH - sFONT_STR_WIDTH(22))
#define FREQUENCIES_XPOS3           (LCD_WIDTH/2 + OFFSETX - sFONT_STR_WIDTH(14) / 2)
#define FREQUENCIES_YPOS            (AREA_HEIGHT_NORMAL + 2)
#endif // end 480x320 display plot definitions

// UI size defines
// Text offset in menu
#define MENU_TEXT_OFFSET              6
#define MENU_ICON_OFFSET              4
// Scale / ref quick touch position
#define UI_SCALE_REF_X0             (OFFSETX - 5)
#define UI_SCALE_REF_X1             (OFFSETX + CELLOFFSETX + 10)
// Leveler Marker mode select
#define UI_MARKER_Y0                 30
// Maximum menu buttons count
#define MENU_BUTTON_MAX              16
#define MENU_BUTTON_MIN               8
// Menu buttons y offset
#define MENU_BUTTON_Y_OFFSET          1
// Menu buttons size = 21 for icon and 10 chars
#define MENU_BUTTON_WIDTH           (7 + FONT_STR_WIDTH(12))
#define MENU_BUTTON_HEIGHT(n)       (AREA_HEIGHT_NORMAL/(n))
#define MENU_BUTTON_BORDER            1
#define KEYBOARD_BUTTON_BORDER        1
#define BROWSER_BUTTON_BORDER         1
// Browser window settings
#define FILES_COLUMNS               (LCD_WIDTH/160)                                // columns in browser
#define FILES_ROWS                   10                                            // rows in browser
#define FILES_PER_PAGE              (FILES_COLUMNS*FILES_ROWS)                     // FILES_ROWS * FILES_COLUMNS
#define FILE_BOTTOM_HEIGHT           20                                            // Height of bottom buttons (< > X)
#define FILE_BUTTON_HEIGHT          ((LCD_HEIGHT - FILE_BOTTOM_HEIGHT)/FILES_ROWS) // Height of file buttons

// Define message box width
#define MESSAGE_BOX_WIDTH           180

// Height of numerical input field (at bottom)
#define NUM_INPUT_HEIGHT             32
// On screen keyboard button size
#if 1 // Full screen keyboard
#define KP_WIDTH                  (LCD_WIDTH / 4)                                  // numeric keypad button width
#define KP_HEIGHT                 ((LCD_HEIGHT - NUM_INPUT_HEIGHT) / 4)            // numeric keypad button height
#define KP_X_OFFSET               0                                                // numeric keypad X offset
#define KP_Y_OFFSET               0                                                // numeric keypad Y offset
#define KPF_WIDTH                 (LCD_WIDTH / 10)                                 // text keypad button width
#define KPF_HEIGHT                KPF_WIDTH                                        // text keypad button height
#define KPF_X_OFFSET              0                                                // text keypad X offset
#define KPF_Y_OFFSET              (LCD_HEIGHT - NUM_INPUT_HEIGHT - 4 * KPF_HEIGHT) // text keypad Y offset
#else // 64 pixel size keyboard
#define KP_WIDTH                 64                                                // numeric keypad button width
#define KP_HEIGHT                64                                                // numeric keypad button height
#define KP_X_OFFSET              (LCD_WIDTH-MENU_BUTTON_WIDTH-16-KP_WIDTH*4)       // numeric keypad X offset
#define KP_Y_OFFSET              20                                                // numeric keypad Y offset
#define KPF_WIDTH                (LCD_WIDTH / 10)                                  // text keypad button width
#define KPF_HEIGHT               KPF_WIDTH                                         // text keypad button height
#define KPF_X_OFFSET              0                                                // text keypad X offset
#define KPF_Y_OFFSET             (LCD_HEIGHT - NUM_INPUT_HEIGHT - 4 * KPF_HEIGHT)  // text keypad Y offset
#endif

#if _USE_FONT_ == 0
extern const uint8_t x5x7_bits[];
#define FONT_START_CHAR   0x16
#define FONT_WIDTH           5
#define FONT_GET_HEIGHT      7
#define FONT_STR_WIDTH(n)    ((n)*FONT_WIDTH)
#define FONT_STR_HEIGHT      8
#define FONT_GET_DATA(ch)    (  &x5x7_bits[(ch-FONT_START_CHAR)*FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch)   (8-(x5x7_bits[(ch-FONT_START_CHAR)*FONT_GET_HEIGHT]&0x7))

#elif _USE_FONT_ == 1
extern const uint8_t x6x10_bits[];
#define FONT_START_CHAR   0x16
#define FONT_WIDTH           6
#define FONT_GET_HEIGHT     10
#define FONT_STR_WIDTH(n)   ((n)*FONT_WIDTH)
#define FONT_STR_HEIGHT     11
#define FONT_GET_DATA(ch)   (  &x6x10_bits[(ch-FONT_START_CHAR)*FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch)  (8-(x6x10_bits[(ch-FONT_START_CHAR)*FONT_GET_HEIGHT]&0x7))

#elif _USE_FONT_ == 2
extern const uint8_t x7x11b_bits[];
#define FONT_START_CHAR   0x16
#define FONT_WIDTH           7
#define FONT_GET_HEIGHT     11
#define FONT_STR_WIDTH(n)   ((n)*FONT_WIDTH)
#define FONT_STR_HEIGHT     11
#define FONT_GET_DATA(ch)   (  &x7x11b_bits[(ch-FONT_START_CHAR)*FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch)  (8-(x7x11b_bits[(ch-FONT_START_CHAR)*FONT_GET_HEIGHT]&7))

#elif _USE_FONT_ == 3
extern const uint8_t x11x14_bits[];
#define FONT_START_CHAR   0x16
#define FONT_WIDTH          11
#define FONT_GET_HEIGHT     14
#define FONT_STR_WIDTH(n)   ((n)*FONT_WIDTH)
#define FONT_STR_HEIGHT     16
#define FONT_GET_DATA(ch)   (   &x11x14_bits[(ch-FONT_START_CHAR)*2*FONT_GET_HEIGHT  ])
#define FONT_GET_WIDTH(ch)  (14-(x11x14_bits[(ch-FONT_START_CHAR)*2*FONT_GET_HEIGHT+1]&0x7))
#endif

#if _USE_SMALL_FONT_ == 0
extern const uint8_t x5x7_bits[];
#define sFONT_START_CHAR   0x16
#define sFONT_WIDTH           5
#define sFONT_GET_HEIGHT      7
#define sFONT_STR_WIDTH(n)    ((n)*sFONT_WIDTH)
#define sFONT_STR_HEIGHT      8
#define sFONT_GET_DATA(ch)    (  &x5x7_bits[(ch-sFONT_START_CHAR)*sFONT_GET_HEIGHT])
#define sFONT_GET_WIDTH(ch)   (8-(x5x7_bits[(ch-sFONT_START_CHAR)*sFONT_GET_HEIGHT]&0x7))

#elif _USE_SMALL_FONT_ == 1
extern const uint8_t x6x10_bits[];
#define sFONT_START_CHAR   0x16
#define sFONT_WIDTH           6
#define sFONT_GET_HEIGHT     10
#define sFONT_STR_WIDTH(n)   ((n)*sFONT_WIDTH)
#define sFONT_STR_HEIGHT     11
#define sFONT_GET_DATA(ch)   (  &x6x10_bits[(ch-sFONT_START_CHAR)*sFONT_GET_HEIGHT])
#define sFONT_GET_WIDTH(ch)  (8-(x6x10_bits[(ch-sFONT_START_CHAR)*sFONT_GET_HEIGHT]&0x7))

#elif _USE_SMALL_FONT_ == 2
extern const uint8_t x7x11b_bits[];
#define sFONT_START_CHAR   0x16
#define sFONT_WIDTH           7
#define sFONT_GET_HEIGHT     11
#define sFONT_STR_WIDTH(n)   ((n)*sFONT_WIDTH)
#define sFONT_STR_HEIGHT     11
#define sFONT_GET_DATA(ch)   (  &x7x11b_bits[(ch-sFONT_START_CHAR)*sFONT_GET_HEIGHT])
#define sFONT_GET_WIDTH(ch)  (8-(x7x11b_bits[(ch-sFONT_START_CHAR)*sFONT_GET_HEIGHT]&7))

#elif _USE_SMALL_FONT_ == 3
extern const uint8_t x11x14_bits[];
#define sFONT_START_CHAR   0x16
#define sFONT_WIDTH          11
#define sFONT_GET_HEIGHT     14
#define sFONT_STR_WIDTH(n)   ((n)*sFONT_WIDTH)
#define sFONT_STR_HEIGHT     16
#define sFONT_GET_DATA(ch)   (   &x11x14_bits[(ch-sFONT_START_CHAR)*2*sFONT_GET_HEIGHT  ])
#define sFONT_GET_WIDTH(ch)  (14-(x11x14_bits[(ch-sFONT_START_CHAR)*2*sFONT_GET_HEIGHT+1]&0x7))
#endif

// Font type defines
enum {FONT_SMALL = 0, FONT_NORMAL};
#if _USE_FONT_ != _USE_SMALL_FONT_
void lcd_set_font(int type);
#else
#define lcd_set_font(type) (void)(type)
#endif

extern const uint8_t numfont16x22[];
#define NUM_FONT_GET_WIDTH      16
#define NUM_FONT_GET_HEIGHT     22
#define NUM_FONT_GET_DATA(ch)   (&numfont16x22[ch*2*NUM_FONT_GET_HEIGHT])

// Glyph names from numfont16x22.c
enum {
  KP_0 = 0, KP_1, KP_2, KP_3, KP_4, KP_5, KP_6, KP_7, KP_8, KP_9,
  KP_PERIOD,
  KP_MINUS,
  KP_BS,
  KP_k, KP_M, KP_G,
  KP_m, KP_u, KP_n, KP_p,
  KP_X1, KP_ENTER, KP_PERCENT, // Enter values
#if 0
  KP_INF,
  KP_DB,
  KP_PLUSMINUS,
  KP_KEYPAD,
  KP_SPACE,
  KP_PLUS,
#endif
  // Special uint8_t buttons
  KP_EMPTY = 255  // Empty button
};

/*
 * LC match text output settings
 */
#ifdef __VNA_MEASURE_MODULE__
// X and Y offset to L/C match text
 #define STR_MEASURE_X      (OFFSETX +  0)
// Better be aligned by cell (cell height = 32)
 #define STR_MEASURE_Y      (OFFSETY + 80)
// 1/3 Width of text (use 3 column for data)
 #define STR_MEASURE_WIDTH  (FONT_WIDTH * 10)
// String Height (need 2 + 0..4 string)
 #define STR_MEASURE_HEIGHT (FONT_STR_HEIGHT + 1)
#endif

#ifdef __USE_GRID_VALUES__
#define GRID_X_TEXT   (WIDTH - sFONT_STR_WIDTH(5))
#endif

enum {
  LCD_BG_COLOR = 0,       // background
  LCD_FG_COLOR,           // foreground (in most cases text on background)
  LCD_GRID_COLOR,         // grid lines color
  LCD_MENU_COLOR,         // UI menu color
  LCD_MENU_TEXT_COLOR,    // UI menu text color
  LCD_MENU_ACTIVE_COLOR,  // UI selected menu color
  LCD_TRACE_1_COLOR,      // Trace 1 color
  LCD_TRACE_2_COLOR,      // Trace 2 color
  LCD_TRACE_3_COLOR,      // Trace 3 color
  LCD_TRACE_4_COLOR,      // Trace 4 color
  LCD_TRACE_5_COLOR,      // Stored trace A color
  LCD_TRACE_6_COLOR,      // Stored trace B color
  LCD_NORMAL_BAT_COLOR,   // Normal battery icon color
  LCD_LOW_BAT_COLOR,      // Low battery icon color
  LCD_SPEC_INPUT_COLOR,   // Not used, for future
  LCD_RISE_EDGE_COLOR,    // UI menu button rise edge color
  LCD_FALLEN_EDGE_COLOR,  // UI menu button fallen edge color
  LCD_SWEEP_LINE_COLOR,   // Sweep line color
  LCD_BW_TEXT_COLOR,      // Bandwidth text color
  LCD_INPUT_TEXT_COLOR,   // Keyboard Input text color
  LCD_INPUT_BG_COLOR,     // Keyboard Input text background color
  LCD_MEASURE_COLOR,      // Measure text color
  LCD_GRID_VALUE_COLOR,   // Not used, for future
  LCD_INTERP_CAL_COLOR,   // Calibration state on interpolation color
  LCD_DISABLE_CAL_COLOR,  // Calibration state on disable color
  LCD_LINK_COLOR,         // UI menu button text for values color
  LCD_TXT_SHADOW_COLOR,   // Plot area text border color
};

#define LCD_DEFAULT_PALETTE {\
[LCD_BG_COLOR         ] = RGB565(  0,  0,  0), \
[LCD_FG_COLOR         ] = RGB565(255,255,255), \
[LCD_GRID_COLOR       ] = RGB565(128,128,128), \
[LCD_MENU_COLOR       ] = RGB565(230,230,230), \
[LCD_MENU_TEXT_COLOR  ] = RGB565(  0,  0,  0), \
[LCD_MENU_ACTIVE_COLOR] = RGB565(210,210,210), \
[LCD_TRACE_1_COLOR    ] = RGB565(255,255,  0), \
[LCD_TRACE_2_COLOR    ] = RGB565(  0,255,255), \
[LCD_TRACE_3_COLOR    ] = RGB565(  0,255,  0), \
[LCD_TRACE_4_COLOR    ] = RGB565(255,  0,255), \
[LCD_TRACE_5_COLOR    ] = RGB565(255,  0,  0), \
[LCD_TRACE_6_COLOR    ] = RGB565(  0,  0,255), \
[LCD_NORMAL_BAT_COLOR ] = RGB565( 31,227,  0), \
[LCD_LOW_BAT_COLOR    ] = RGB565(255,  0,  0), \
[LCD_SPEC_INPUT_COLOR ] = RGB565(128,255,128), \
[LCD_RISE_EDGE_COLOR  ] = RGB565(255,255,255), \
[LCD_FALLEN_EDGE_COLOR] = RGB565(128,128,128), \
[LCD_SWEEP_LINE_COLOR ] = RGB565(  0,  0,255), \
[LCD_BW_TEXT_COLOR    ] = RGB565(196,196,196), \
[LCD_INPUT_TEXT_COLOR ] = RGB565(  0,  0,  0), \
[LCD_INPUT_BG_COLOR   ] = RGB565(255,255,255), \
[LCD_MEASURE_COLOR    ] = RGB565(255,255,255), \
[LCD_GRID_VALUE_COLOR ] = RGB565( 96, 96, 96), \
[LCD_INTERP_CAL_COLOR ] = RGB565( 31,227,  0), \
[LCD_DISABLE_CAL_COLOR] = RGB565(255,  0,  0), \
[LCD_LINK_COLOR       ] = RGB565(  0,  0,192), \
[LCD_TXT_SHADOW_COLOR ] = RGB565(  0,  0,  0), \
}


// Plot Redraw flags.
#define REDRAW_PLOT       (1<< 0) // Update all trace indexes in plot area
#define REDRAW_AREA       (1<< 1) // Redraw all plot area
#define REDRAW_CELLS      (1<< 2) // Redraw only updated cells
#define REDRAW_FREQUENCY  (1<< 3) // Redraw Start/Stop/Center/Span frequency, points count, Avg/IFBW
#define REDRAW_CAL_STATUS (1<< 4) // Redraw calibration status (left screen part)
#define REDRAW_MARKER     (1<< 5) // Redraw marker plates and text
#define REDRAW_REFERENCE  (1<< 6) // Redraw reference
#define REDRAW_GRID_VALUE (1<< 7) // Redraw grid values
#define REDRAW_BATTERY    (1<< 8) // Redraw battery state
#define REDRAW_CLRSCR     (1<< 9) // Clear all screen before redraw
#define REDRAW_BACKUP     (1<<10) // Update backup information

// Set this if need update all screen
#define REDRAW_ALL   (REDRAW_CLRSCR | REDRAW_AREA | REDRAW_CAL_STATUS | REDRAW_BATTERY | REDRAW_FREQUENCY)

#define SWEEP_ENABLE  0x01
#define SWEEP_ONCE    0x02
#define SWEEP_BINARY  0x08
#define SWEEP_REMOTE  0x40
#define SWEEP_UI_MODE 0x80
