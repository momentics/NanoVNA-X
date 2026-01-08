/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "ch.h"
#include "hal.h"
#include "nanovna.h"
#include "sys/shell_service.h"
#include "driver/board_events.h"
#include "ui/menus/menu_storage.h"
#include "ui/menus/menu_system.h" // For menu_vna_mode_acb
#include "ui/menus/menu_main.h"   // For menu_stored_trace_acb if applicable
#include "ui/core/ui_core.h"
#include "ui/core/ui_menu_engine.h"
#include "ui/core/ui_keypad.h"
#include "chprintf.h"
#include <string.h>
#include "sys/config_service.h"
#include "sys/state_manager.h" // For state_manager_force_save if needed
#include "driver/board_events.h" // For boardDFUEnter if referenced? No, local DFU is System.

#ifdef __USE_SD_CARD__

#include "ff.h" // FatFs

// Use size optimization
#pragma GCC optimize("Os")

static uint8_t keyboard_temp; // Used for SD card keyboard workflows

// Save file callback
typedef FRESULT (*file_save_cb_t)(FIL* f, uint8_t format);
#define FILE_SAVE_CALLBACK(save_function_name) FRESULT save_function_name(FIL* f, uint8_t format)
// Load file callback
typedef const char* (*file_load_cb_t)(FIL* f, FILINFO* fno, uint8_t format);
#define FILE_LOAD_CALLBACK(load_function_name)                                                     \
  const char* load_function_name(FIL* f, FILINFO* fno, uint8_t format)

//=====================================================================================================
// S1P and S2P file headers, and data structures
//=====================================================================================================
static const char s1_file_header[] = "!File created by NanoVNA\r\n"
                                     "# Hz S RI R 50\r\n";

static const char s1_file_param[] = "%u % f % f\r\n";

static const char s2_file_header[] = "!File created by NanoVNA\r\n"
                                     "# Hz S RI R 50\r\n";

static const char s2_file_param[] = "%u % f % f % f % f 0 0 0 0\r\n";

static FILE_SAVE_CALLBACK(save_snp) {
  const char* s_file_format;
  char* buf_8 = (char*)spi_buffer;
  FRESULT res;
  UINT size;
  if (format == FMT_S1P_FILE) {
    s_file_format = s1_file_param;
    res = f_write(f, s1_file_header, sizeof(s1_file_header) - 1, &size);
  } else {
    s_file_format = s2_file_param;
    res = f_write(f, s2_file_header, sizeof(s2_file_header) - 1, &size);
  }
  for (int i = 0; i < sweep_points && res == FR_OK; i++) {
    size = plot_printf(buf_8, 128, s_file_format, get_frequency(i), measured[0][i][0],
                       measured[0][i][1], measured[1][i][0], measured[1][i][1]);
    res = f_write(f, buf_8, size, &size);
  }
  return res;
}

static FILE_LOAD_CALLBACK(load_snp) {
  (void)fno;
  UINT size;
  const int buffer_size = 256;
  const int line_size = 128;
  char* buf_8 = (char*)spi_buffer;
  char* line = buf_8 + buffer_size;
  uint16_t j = 0, i, count = 0;
  freq_t start = 0, stop = 0, freq;
  while (f_read(f, buf_8, buffer_size, &size) == FR_OK && size > 0) {
    for (i = 0; i < size; i++) {
      uint8_t c = buf_8[i];
      if (c == '\r') {
        line[j] = 0;
        j = 0;
        char* args[16];
        int nargs = parse_line(line, args, 16);
        if (nargs < 2 || args[0][0] == '#' || args[0][0] == '!')
          continue;
        freq = my_atoui(args[0]);
        if (count >= SWEEP_POINTS_MAX || freq > FREQUENCY_MAX)
          return "Format err";
        if (count == 0)
          start = freq;
        stop = freq;
        measured[0][count][0] = my_atof(args[1]);
        measured[0][count][1] = my_atof(args[2]);
        if (format == FMT_S2P_FILE && nargs >= 4) {
          measured[1][count][0] = my_atof(args[3]);
          measured[1][count][1] = my_atof(args[4]);
        } else {
          measured[1][count][0] = 0.0f;
          measured[1][count][1] = 0.0f;
        }
        count++;
      } else if (c < 0x20)
        continue;
      else if (j < line_size)
        line[j++] = (char)c;
    }
  }
  if (count != 0) {
    pause_sweep();
    current_props._electrical_delay[0] = 0.0f;
    current_props._electrical_delay[1] = 0.0f;
    current_props._sweep_points = count;
    set_sweep_frequency(ST_START, start);
    set_sweep_frequency(ST_STOP, stop);
    request_to_redraw(REDRAW_PLOT);
  }
  return NULL;
}

//=====================================================================================================
// Bitmap file header for LCD_WIDTH x LCD_HEIGHT image 16bpp (v4 format allow set RGB mask)
//=====================================================================================================
#define BMP_UINT32(val)                                                                            \
  ((val) >> 0) & 0xFF, ((val) >> 8) & 0xFF, ((val) >> 16) & 0xFF, ((val) >> 24) & 0xFF
#define BMP_UINT16(val) ((val) >> 0) & 0xFF, ((val) >> 8) & 0xFF
#define BMP_H1_SIZE (14)
#define BMP_V4_SIZE (108)
#define BMP_HEAD_SIZE (BMP_H1_SIZE + BMP_V4_SIZE)
#define BMP_SIZE (2 * LCD_WIDTH * LCD_HEIGHT)
#define BMP_FILE_SIZE (BMP_SIZE + BMP_HEAD_SIZE)
static const uint8_t bmp_header_v4[BMP_H1_SIZE + BMP_V4_SIZE] = {
    0x42, 0x4D, BMP_UINT32(BMP_FILE_SIZE), BMP_UINT16(0), BMP_UINT16(0), BMP_UINT32(BMP_HEAD_SIZE),
    BMP_UINT32(BMP_V4_SIZE), BMP_UINT32(LCD_WIDTH), BMP_UINT32(LCD_HEIGHT), BMP_UINT16(1),
    BMP_UINT16(16), BMP_UINT32(3), BMP_UINT32(BMP_SIZE), BMP_UINT32(0x0EC4), BMP_UINT32(0x0EC4),
    BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0b1111100000000000), BMP_UINT32(0b0000011111100000),
    BMP_UINT32(0b0000000000011111), BMP_UINT32(0b0000000000000000), 'B', 'G', 'R', 's',
    BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0),
    BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0), BMP_UINT32(0)};

static FILE_SAVE_CALLBACK(save_bmp) {
  (void)format;
  UINT size;
  uint16_t* buf_16 = (uint16_t*)spi_buffer;
  FRESULT res = f_write(f, bmp_header_v4, sizeof(bmp_header_v4), &size);
  lcd_set_background(LCD_SWEEP_LINE_COLOR);
  for (int y = LCD_HEIGHT - 1; y >= 0 && res == FR_OK; y--) {
    lcd_read_memory(0, y, LCD_WIDTH, 1, buf_16);
    swap_bytes(buf_16, LCD_WIDTH);
    res = f_write(f, buf_16, LCD_WIDTH * sizeof(uint16_t), &size);
    lcd_fill(LCD_WIDTH - 1, y, 1, 1);
  }
  return res;
}

static FILE_LOAD_CALLBACK(load_bmp) {
  (void)format;
  UINT size;
  uint16_t* buf_16 = (uint16_t*)spi_buffer;
  FRESULT res = f_read(f, (void*)buf_16, sizeof(bmp_header_v4), &size);
  if (res != FR_OK || buf_16[9] != LCD_WIDTH || buf_16[11] != LCD_HEIGHT || buf_16[14] != 16)
    return "Format err";
  for (int y = LCD_HEIGHT - 1; y >= 0 && res == FR_OK; y--) {
    res = f_read(f, (void*)buf_16, LCD_WIDTH * sizeof(uint16_t), &size);
    swap_bytes(buf_16, LCD_WIDTH);
    lcd_bulk(0, y, LCD_WIDTH, 1);
  }
  lcd_printf(0, LCD_HEIGHT - 3 * FONT_STR_HEIGHT, fno->fname);
  return NULL;
}

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

static const uint8_t tif_header[] = {
    0x49, 0x49, BMP_UINT16(0x002A), BMP_UINT32(0x0008), BMP_UINT16(IFD_ENTRIES_COUNT),
    IFD_ENTRY(256, IFD_LONG, 1, LCD_WIDTH), IFD_ENTRY(257, IFD_LONG, 1, LCD_HEIGHT),
    IFD_ENTRY(258, IFD_SHORT, 3, IFD_BPS_OFFSET), IFD_ENTRY(259, IFD_SHORT, 1, TIFF_PACKBITS),
    IFD_ENTRY(262, IFD_SHORT, 1, TIFF_PHOTOMETRIC_RGB), IFD_ENTRY(273, IFD_LONG, 1, IFD_STRIP_OFFSET),
    IFD_ENTRY(277, IFD_SHORT, 1, 3), BMP_UINT32(0)};

static FILE_SAVE_CALLBACK(save_tiff) {
  (void)format;
  UINT size;
  uint16_t* buf_16 = (uint16_t*)spi_buffer;
  char* buf_8;
  FRESULT res = f_write(f, tif_header, sizeof(tif_header), &size);
  lcd_set_background(LCD_SWEEP_LINE_COLOR);
  for (int y = 0; y < LCD_HEIGHT && res == FR_OK; y++) {
    buf_8 = (char*)buf_16 + 128;
    lcd_read_memory(0, y, LCD_WIDTH, 1, buf_16);
    for (int x = LCD_WIDTH - 1; x >= 0; x--) {
      uint16_t color = (buf_16[x] << 8) | (buf_16[x] >> 8);
      buf_8[3 * x + 0] = (color >> 8) & 0xF8;
      buf_8[3 * x + 1] = (color >> 3) & 0xFC;
      buf_8[3 * x + 2] = (color << 3) & 0xF8;
    }
    size = packbits(buf_8, (char*)buf_16, LCD_WIDTH * 3);
    res = f_write(f, buf_16, size, &size);
    lcd_fill(LCD_WIDTH - 1, y, 1, 1);
  }
  return res;
}

static FILE_LOAD_CALLBACK(load_tiff) {
  (void)format;
  UINT size;
  uint8_t* buf_8 = (uint8_t*)spi_buffer;
  uint16_t* buf_16 = (uint16_t*)spi_buffer;
  FRESULT res = f_read(f, (void*)buf_16, sizeof(tif_header), &size);
  if (res != FR_OK || buf_16[0] != 0x4949 || buf_16[9] != LCD_WIDTH || buf_16[15] != LCD_HEIGHT ||
      buf_16[27] != TIFF_PACKBITS)
    return "Format err";
  for (int y = 0; y < LCD_HEIGHT && res == FR_OK; y++) {
    for (int x = 0; x < LCD_WIDTH * 3;) {
      int8_t data[2];
      res = f_read(f, data, 2, &size);
      int count = data[0];
      buf_8[x++] = data[1];
      if (count > 0) {
        res = f_read(f, &buf_8[x], count, &size);
        x += count;
      } else
        while (count++ < 0)
          buf_8[x++] = data[1];
    }
    for (int x = 0; x < LCD_WIDTH; x++)
      buf_16[x] = RGB565(buf_8[3 * x + 0], buf_8[3 * x + 1], buf_8[3 * x + 2]);
    lcd_bulk(0, y, LCD_WIDTH, 1);
  }
  lcd_printf(0, LCD_HEIGHT - 3 * FONT_STR_HEIGHT, fno->fname);
  return NULL;
}
#endif

static FILE_SAVE_CALLBACK(save_cal) {
  (void)format;
  UINT size;
  const char* src = (char*)&current_props;
  const uint32_t total = sizeof(current_props);
  return f_write(f, src, total, &size);
}

static FILE_LOAD_CALLBACK(load_cal) {
  (void)format;
  UINT size;
  uint32_t magic;
  char* src = (char*)&current_props + sizeof(magic);
  uint32_t total = sizeof(current_props) - sizeof(magic);
  if (fno->fsize != sizeof(current_props) || f_read(f, &magic, sizeof(magic), &size) != FR_OK ||
      magic != PROPERTIES_MAGIC || f_read(f, src, total, &size) != FR_OK)
    return "Format err";
  load_properties(NO_SAVE_SLOT);
  return NULL;
}

#ifdef __SD_CARD_DUMP_FIRMWARE__
static FILE_SAVE_CALLBACK(save_bin) {
  (void)format;
  UINT size;
  const char* src = (const char*)FLASH_START_ADDRESS;
  const uint32_t total = FLASH_TOTAL_SIZE;
  return f_write(f, src, total, &size);
}
#endif

#ifdef __SD_CARD_LOAD__
static FILE_LOAD_CALLBACK(load_cmd) {
  (void)fno;
  (void)format;
  UINT size;
  const int buffer_size = 256;
  const int line_size = 128;
  char* buf_8 = (char*)spi_buffer;
  char* line = buf_8 + buffer_size;
  uint16_t j = 0, i;
  while (f_read(f, buf_8, buffer_size, &size) == FR_OK && size > 0) {
    for (i = 0; i < size; i++) {
      uint8_t c = buf_8[i];
      if (c == '\r') {
        line[j] = 0;
        j = 0;
        vna_shell_execute_cmd_line(line);
      } else if (c < 0x20)
        continue;
      else if (j < line_size)
        line[j++] = (char)c;
    }
  }
  if (j > 0) {
    line[j] = 0;
    vna_shell_execute_cmd_line(line);
  }
  return NULL;
}
#endif

#if defined(__USE_SD_CARD__) && FF_USE_MKFS
_Static_assert(sizeof(spi_buffer) >= FF_MAX_SS, "spi_buffer is too small for mkfs work buffer");

static FRESULT sd_card_format(void) {
  BYTE* work = (BYTE*)spi_buffer;
  FATFS* fs = filesystem_volume();
  f_mount(NULL, "", 0);
  DSTATUS status = disk_initialize(0);
  if (status & STA_NOINIT)
    return FR_NOT_READY;
  /* Allow mkfs to pick FAT12/16 for small cards and FAT32 for larger media. */
  MKFS_PARM opt = {.fmt = FM_FAT | FM_FAT32, .n_fat = 1, .align = 0, .n_root = 0, .au_size = 0};
  FRESULT res = f_mkfs("", &opt, work, FF_MAX_SS);
  if (res != FR_OK)
    return res;
  disk_ioctl(0, CTRL_SYNC, NULL);
  memset(fs, 0, sizeof(*fs));
  return f_mount(fs, "", 1);
}

static UI_FUNCTION_CALLBACK(menu_sdcard_format_cb) {
  (void)data;
  bool resume = (sweep_mode & SWEEP_ENABLE) != 0;
  if (resume)
    toggle_sweep();
  systime_t start = chVTGetSystemTimeX();
  ui_message_box_draw("FORMAT SD", "Formatting...");
  chThdSleepMilliseconds(120);
  FRESULT result = sd_card_format();
  if (resume)
    toggle_sweep();
  char msg[32];
  FRESULT res = result;
  if (res == FR_OK) {
    uint32_t elapsed_ms = (uint32_t)ST2MS(chVTTimeElapsedSinceX(start));
    plot_printf(msg, sizeof(msg), "OK %lums", (unsigned long)elapsed_ms);
  } else
    plot_printf(msg, sizeof(msg), "ERR %d", res);
  ui_message_box("FORMAT SD", msg, 2000);
  ui_mode_normal();
}
#endif

#ifdef __SD_FILE_BROWSER__
#define FILE_OPTIONS(e, s, l, o) {e, s, l, o}
#else
#define FILE_OPTIONS(e, s, l, o) {e, s, o}
#endif

#define FILE_OPT_REDRAW (1 << 0)
#define FILE_OPT_CONTINUE (1 << 1)

const struct {
  const char* ext;
  file_save_cb_t save;
#ifdef __SD_FILE_BROWSER__
  file_load_cb_t load;
#endif
  uint32_t opt;
} file_opt[] = {
    [FMT_S1P_FILE] = FILE_OPTIONS("s1p", save_snp, load_snp, 0),
    [FMT_S2P_FILE] = FILE_OPTIONS("s2p", save_snp, load_snp, 0),
    [FMT_BMP_FILE] = FILE_OPTIONS("bmp", save_bmp, load_bmp, FILE_OPT_REDRAW | FILE_OPT_CONTINUE),
#ifdef __SD_CARD_DUMP_TIFF__
    [FMT_TIF_FILE] = FILE_OPTIONS("tif", save_tiff, load_tiff, FILE_OPT_REDRAW | FILE_OPT_CONTINUE),
#endif
    [FMT_CAL_FILE] = FILE_OPTIONS("cal", save_cal, load_cal, 0),
#ifdef __SD_CARD_DUMP_FIRMWARE__
    [FMT_BIN_FILE] = FILE_OPTIONS("bin", save_bin, NULL, 0),
#endif
#ifdef __SD_CARD_LOAD__
    [FMT_CMD_FILE] = FILE_OPTIONS("cmd", NULL, load_cmd, 0),
#endif
};

static FRESULT ui_create_file(char* fs_filename) {
  FRESULT res = f_mount(filesystem_volume(), "", 1);
  if (res != FR_OK)
    return res;
  FIL* const file = filesystem_file();
  res = f_open(file, fs_filename, FA_CREATE_ALWAYS | FA_READ | FA_WRITE);
  return res;
}

static void ui_save_file(char* name, uint8_t format) {
  char fs_filename[FF_LFN_BUF];
  file_save_cb_t save = file_opt[format].save;
  if (save == NULL)
    return;
  if (ui_mode != UI_NORMAL && (file_opt[format].opt & FILE_OPT_REDRAW)) {
    ui_mode_normal();
    draw_all();
  }

  if (name == NULL) {
#if FF_USE_LFN >= 1
    uint32_t tr = rtc_get_tr_bcd();
    uint32_t dr = rtc_get_dr_bcd();
    plot_printf(fs_filename, FF_LFN_BUF, "VNA_%06x_%06x.%s", dr, tr, file_opt[format].ext);
#else
    plot_printf(fs_filename, FF_LFN_BUF, "%08x.%s", rtc_get_fat(), file_opt[format].ext);
#endif
  } else
    plot_printf(fs_filename, FF_LFN_BUF, "%s.%s", name, file_opt[format].ext);

  FRESULT res = ui_create_file(fs_filename);
  if (res == FR_OK) {
    FIL* const file = filesystem_file();
    res = save(file, format);
    f_close(file);
  }
  if (keyboard_temp == 1)
    toggle_sweep();
  ui_message_box("SD CARD SAVE", res == FR_OK ? fs_filename : "  Fail write  ", 2000);
  request_to_redraw(REDRAW_AREA | REDRAW_FREQUENCY);
  ui_mode_normal();
}

static uint16_t fix_screenshot_format(uint16_t data) {
#ifdef __SD_CARD_DUMP_TIFF__
  if (data == FMT_BMP_FILE && VNA_MODE(VNA_MODE_TIFF))
    return FMT_TIF_FILE;
#endif
  return data;
}

#ifdef __SD_FILE_BROWSER__
#include "vna_browser.c"

static UI_FUNCTION_CALLBACK(menu_sdcard_browse_cb) {
  data = fix_screenshot_format(data);
  ui_mode_browser(data);
}
#endif

UI_KEYBOARD_CALLBACK(input_filename) {
  if (b)
    return;
  ui_save_file(kp_buf, data);
}

UI_FUNCTION_CALLBACK(menu_sdcard_cb) {
  keyboard_temp = (sweep_mode & SWEEP_ENABLE) ? 1 : 0;
  if (keyboard_temp)
    toggle_sweep();
  data = fix_screenshot_format(data);
  if (VNA_MODE(VNA_MODE_AUTO_NAME))
    ui_save_file(NULL, data);
  else
    ui_mode_keypad(data + KM_S1P_NAME);
}

#ifdef __SD_FILE_BROWSER__
const menuitem_t menu_sdcard_browse[] = {
    {MT_CALLBACK, FMT_BMP_FILE, "LOAD\nSCREENSHOT", menu_sdcard_browse_cb},
    {MT_CALLBACK, FMT_S1P_FILE, "LOAD S1P", menu_sdcard_browse_cb},
    {MT_CALLBACK, FMT_S2P_FILE, "LOAD S2P", menu_sdcard_browse_cb},
    {MT_CALLBACK, FMT_CAL_FILE, "LOAD CAL", menu_sdcard_browse_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

const menuitem_t menu_sdcard[] = {
#ifdef __SD_FILE_BROWSER__
    {MT_SUBMENU, 0, "LOAD", menu_sdcard_browse},
#endif
    {MT_CALLBACK, FMT_S1P_FILE, "SAVE S1P", menu_sdcard_cb},
    {MT_CALLBACK, FMT_S2P_FILE, "SAVE S2P", menu_sdcard_cb},
    {MT_CALLBACK, FMT_BMP_FILE, "SCREENSHOT", menu_sdcard_cb},
    {MT_CALLBACK, FMT_CAL_FILE, "SAVE\nCALIBRATION", menu_sdcard_cb},
#if FF_USE_MKFS
    {MT_CALLBACK, 0, "FORMAT SD", menu_sdcard_format_cb},
#endif
    {MT_ADV_CALLBACK, VNA_MODE_AUTO_NAME, "AUTO NAME", menu_vna_mode_acb},
#ifdef __SD_CARD_DUMP_TIFF__
    {MT_ADV_CALLBACK, VNA_MODE_TIFF, "IMAGE FORMAT\n " R_LINK_COLOR "%s", menu_vna_mode_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

#endif // __USE_SD_CARD__
