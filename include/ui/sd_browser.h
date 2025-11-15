/*
 * SD browser interface
 */

#pragma once

#include "nanovna.h"
#include "ui/ui_internal.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __USE_SD_CARD__

enum {
  FMT_S1P_FILE = 0,
  FMT_S2P_FILE,
  FMT_BMP_FILE,
#ifdef __SD_CARD_DUMP_TIFF__
  FMT_TIF_FILE,
#endif
  FMT_CAL_FILE,
#ifdef __SD_CARD_DUMP_FIRMWARE__
  FMT_BIN_FILE,
#endif
#ifdef __SD_CARD_LOAD__
  FMT_CMD_FILE,
#endif
};

typedef FRESULT (*file_save_cb_t)(FIL* f, uint8_t format);
typedef const char* (*file_load_cb_t)(FIL* f, FILINFO* fno, uint8_t format);

#define FILE_SAVE_CALLBACK(save_function_name) FRESULT save_function_name(FIL* f, uint8_t format)
#define FILE_LOAD_CALLBACK(load_function_name)                                                     \
  const char* load_function_name(FIL* f, FILINFO* fno, uint8_t format)

typedef struct {
  const char* ext;
  file_save_cb_t save;
  file_load_cb_t load;
  uint32_t opt;
} sd_file_format_t;

extern const sd_file_format_t file_opt[];

#define FILE_OPT_REDRAW (1U << 0)
#define FILE_OPT_CONTINUE (1U << 1)

uint16_t fix_screenshot_format(uint16_t data);

typedef struct {
  uint8_t* data;
  size_t size;
  bool using_measurement;
} sd_temp_buffer_t;

bool sd_temp_buffer_acquire(size_t required_bytes, sd_temp_buffer_t* handle);
void sd_temp_buffer_release(sd_temp_buffer_t* handle);

#define BMP_UINT32(val)                                                                            \
  ((val) >> 0) & 0xFF, ((val) >> 8) & 0xFF, ((val) >> 16) & 0xFF, ((val) >> 24) & 0xFF
#define BMP_UINT16(val) ((val) >> 0) & 0xFF, ((val) >> 8) & 0xFF
#define BMP_H1_SIZE (14)
#define BMP_V4_SIZE (108)
#define BMP_HEAD_SIZE (BMP_H1_SIZE + BMP_V4_SIZE)
#define BMP_SIZE (2 * LCD_WIDTH * LCD_HEIGHT)
#define BMP_FILE_SIZE (BMP_SIZE + BMP_HEAD_SIZE)

#ifdef __SD_CARD_DUMP_TIFF__
#define IFD_ENTRY(type, val_t, count, value)                                                       \
  BMP_UINT16(type), BMP_UINT16(val_t), BMP_UINT32(count), BMP_UINT32(value)
#define IFD_BYTE 1
#define IFD_SHORT 3
#define IFD_LONG 4
#define IFD_RATIONAL 5
#define TIFF_PACKBITS 0x8005
#define TIFF_PHOTOMETRIC_RGB 2
#define TIFF_RESUNIT_NONE 1
#define IFD_ENTRIES_COUNT 7
#define IFD_DATA_OFFSET (10 + 12 * IFD_ENTRIES_COUNT + 4)
#define IFD_BPS_OFFSET IFD_DATA_OFFSET
#define IFD_STRIP_OFFSET IFD_DATA_OFFSET + 6
#define TIFF_HEADER_SIZE (10 + 12 * IFD_ENTRIES_COUNT + 4)
#endif

#if SD_BROWSER_ENABLED
void sd_browser_enter(uint16_t format);
void ui_browser_lever(uint16_t status);
void ui_browser_touch(int touch_x, int touch_y);
UI_FUNCTION_CALLBACK(menu_sdcard_browse_cb);
extern const menuitem_t menu_sdcard_browse[];
FILE_LOAD_CALLBACK(load_snp);
FILE_LOAD_CALLBACK(load_bmp);
#ifdef __SD_CARD_DUMP_TIFF__
FILE_LOAD_CALLBACK(load_tiff);
#endif
FILE_LOAD_CALLBACK(load_cal);
#ifdef __SD_CARD_LOAD__
FILE_LOAD_CALLBACK(load_cmd);
#endif
void ui_mode_browser(int mode);
#endif

#endif
