/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
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
#include "ui/sd_browser.h"
#include "app/shell.h"

#if SD_BROWSER_ENABLED

#include "ui/input_adapters/hardware_input.h"

#include <string.h>

static uint16_t file_count;
static uint16_t page_count;
static uint16_t current_page;
static uint16_t browser_mode;
static uint16_t browser_format;
static int8_t browser_selection = -1;

FILE_LOAD_CALLBACK(load_snp) {
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

FILE_LOAD_CALLBACK(load_bmp) {
  (void)format;
  UINT size;
  sd_temp_buffer_t workspace;
  size_t required = LCD_WIDTH * sizeof(uint16_t);
  if (!sd_temp_buffer_acquire(required, &workspace)) {
    return "No workspace";
  }
  uint16_t* buf_16 = (uint16_t*)workspace.data;
  FRESULT res = f_read(f, (void*)buf_16, BMP_HEAD_SIZE, &size);
  if (res != FR_OK || buf_16[9] != LCD_WIDTH || buf_16[11] != LCD_HEIGHT || buf_16[14] != 16) {
    sd_temp_buffer_release(&workspace);
    return "Format err";
  }
  for (int y = LCD_HEIGHT - 1; y >= 0 && res == FR_OK; y--) {
    res = f_read(f, (void*)buf_16, LCD_WIDTH * sizeof(uint16_t), &size);
    swap_bytes(buf_16, LCD_WIDTH);
    lcd_bulk(0, y, LCD_WIDTH, 1);
  }
  lcd_printf(0, LCD_HEIGHT - 3 * FONT_STR_HEIGHT, fno->fname);
  sd_temp_buffer_release(&workspace);
  return NULL;
}

#ifdef __SD_CARD_DUMP_TIFF__
FILE_LOAD_CALLBACK(load_tiff) {
  (void)format;
  UINT size;
  size_t raw_required = LCD_WIDTH * sizeof(uint16_t);
  size_t packed_required = LCD_WIDTH * 3;
  size_t required = raw_required > packed_required ? raw_required : packed_required;
  sd_temp_buffer_t workspace;
  if (!sd_temp_buffer_acquire(required, &workspace)) {
    return "No workspace";
  }
  uint8_t* buf_8 = workspace.data;
  uint16_t* buf_16 = (uint16_t*)workspace.data;
  FRESULT res = f_read(f, (void*)buf_16, TIFF_HEADER_SIZE, &size);
  if (res != FR_OK || buf_16[0] != 0x4949 || buf_16[9] != LCD_WIDTH || buf_16[15] != LCD_HEIGHT ||
      buf_16[27] != TIFF_PACKBITS) {
    sd_temp_buffer_release(&workspace);
    return "Format err";
  }
  for (int y = 0; y < LCD_HEIGHT && res == FR_OK; y++) {
    for (int x = 0; x < LCD_WIDTH * 3;) {
      int8_t data[2];
      res = f_read(f, data, 2, &size);
      int count = data[0];
      buf_8[x++] = data[1];
      if (count > 0) {
        res = f_read(f, &buf_8[x], count, &size);
        x += count;
      } else {
        while (count++ < 0)
          buf_8[x++] = data[1];
      }
    }
    for (int x = 0; x < LCD_WIDTH; x++)
      buf_16[x] = RGB565(buf_8[3 * x + 0], buf_8[3 * x + 1], buf_8[3 * x + 2]);
    lcd_bulk(0, y, LCD_WIDTH, 1);
  }
  lcd_printf(0, LCD_HEIGHT - 3 * FONT_STR_HEIGHT, fno->fname);
  sd_temp_buffer_release(&workspace);
  return NULL;
}
#endif

FILE_LOAD_CALLBACK(load_cal) {
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

#ifdef __SD_CARD_LOAD__
FILE_LOAD_CALLBACK(load_cmd) {
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

UI_FUNCTION_CALLBACK(menu_sdcard_browse_cb) {
  data = fix_screenshot_format(data);
  ui_mode_browser(data);
}

#define BROWSER_DELETE 1

// Buttons in browser
enum {
  FILE_BUTTON_LEFT = 0,
  FILE_BUTTON_RIGHT,
  FILE_BUTTON_EXIT,
  FILE_BUTTON_DEL,
  FILE_BUTTON_FILE
};

#define SMALL_BUTTON_SIZE FONT_STR_WIDTH(6)
// Button position on screen
typedef struct {
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint8_t h;
  uint8_t ofs;
} browser_btn_t;
static const browser_btn_t browser_btn[] = {
    [FILE_BUTTON_LEFT] = {0 + SMALL_BUTTON_SIZE, LCD_HEIGHT - FILE_BOTTOM_HEIGHT,
                          LCD_WIDTH / 2 - 2 * SMALL_BUTTON_SIZE, FILE_BOTTOM_HEIGHT,
                          (LCD_WIDTH / 2 - 2 * SMALL_BUTTON_SIZE - FONT_WIDTH) / 2}, // < previous
    [FILE_BUTTON_RIGHT] = {LCD_WIDTH / 2 + SMALL_BUTTON_SIZE, LCD_HEIGHT - FILE_BOTTOM_HEIGHT,
                           LCD_WIDTH / 2 - 2 * SMALL_BUTTON_SIZE, FILE_BOTTOM_HEIGHT,
                           (LCD_WIDTH / 2 - 2 * SMALL_BUTTON_SIZE - FONT_WIDTH) / 2}, // > next
    [FILE_BUTTON_EXIT] = {LCD_WIDTH - SMALL_BUTTON_SIZE, LCD_HEIGHT - FILE_BOTTOM_HEIGHT,
                          SMALL_BUTTON_SIZE, FILE_BOTTOM_HEIGHT,
                          (SMALL_BUTTON_SIZE - FONT_WIDTH) / 2}, // X exit
    [FILE_BUTTON_DEL] = {0 + 0, LCD_HEIGHT - FILE_BOTTOM_HEIGHT, SMALL_BUTTON_SIZE,
                         FILE_BOTTOM_HEIGHT, (SMALL_BUTTON_SIZE - 3 * FONT_WIDTH) / 2}, // DEL
    // File button, only size and start position, must be idx = FILE_BUTTON_FILE
    [FILE_BUTTON_FILE] = {0, 0, LCD_WIDTH / FILES_COLUMNS, FILE_BUTTON_HEIGHT, FONT_WIDTH / 2 + 3},
};

static void browser_get_button_pos(int idx, browser_btn_t* b) {
  int n = idx >= FILE_BUTTON_FILE ? FILE_BUTTON_FILE : idx;
#if 0
  memcpy(b, &browser_btn[n], sizeof(browser_btn_t));
#else
  b->x = browser_btn[n].x;
  b->y = browser_btn[n].y;
  b->w = browser_btn[n].w;
  b->h = browser_btn[n].h;
  b->ofs = browser_btn[n].ofs;
#endif
  if (idx > FILE_BUTTON_FILE) { // for file buttons use multiplier from start offset
    idx -= FILE_BUTTON_FILE;
    b->x += b->w * (idx / FILES_ROWS);
    b->y += b->h * (idx % FILES_ROWS);
  }
}

static void browser_draw_button(int idx, const char* txt) {
  if (idx < 0)
    return;
  button_t b;
  browser_btn_t btn;
  browser_get_button_pos(idx, &btn);
  // Mark DEL button in file delete mode
  b.bg = (idx == FILE_BUTTON_DEL && (browser_mode & BROWSER_DELETE)) ? LCD_LOW_BAT_COLOR
                                                                     : LCD_MENU_COLOR;
  b.fg = LCD_MENU_TEXT_COLOR;
  b.border = (idx == browser_selection) ? BROWSER_BUTTON_BORDER | BUTTON_BORDER_FALLING
                                        : BROWSER_BUTTON_BORDER | BUTTON_BORDER_RISE;
  if (txt == NULL)
    b.border |= BUTTON_BORDER_NO_FILL;
  ui_draw_button(btn.x, btn.y, btn.w, btn.h, &b);
  if (txt)
    lcd_printf(btn.x + btn.ofs, btn.y + (btn.h - FONT_STR_HEIGHT) / 2, txt);
}

static bool compare_ext(const char* name, const char* ext) {
  int i = 0, j = 0;
  while (name[i])
    if (name[i++] == '.')
      j = i;                                      // Get last '.' position + 1
  return j == 0 ? false : strcmpi(&name[j], ext); // Compare text after '.' and ext
}

static FRESULT sd_findnext(DIR* dp, FILINFO* fno) {
  while (f_readdir(dp, fno) == FR_OK && fno->fname[0]) {
    if (fno->fattrib & AM_DIR)
      continue;
    if (compare_ext(fno->fname, dp->pat))
      return FR_OK;
    // #if FF_USE_LFN && FF_USE_FIND == 2
    //     if (compare_ext(fno->altname, dp->pat)) return FR_OK;
    // #endif
  }
  return FR_NO_FILE;
}

static FRESULT sd_open_dir(DIR* dp, const TCHAR* path, const TCHAR* pattern) {
  dp->pat = pattern;
  return f_opendir(dp, path);
}

static void browser_open_file(int sel) {
  FILINFO fno;
  DIR dj;
  int cnt;
  if ((uint16_t)sel >= file_count)
    return;
  if (f_mount(filesystem_volume(), "", 1) != FR_OK)
    return;
repeat:
  cnt = sel;
  if (sd_open_dir(&dj, "", file_opt[browser_format].ext) != FR_OK)
    return; // open dir
  while (sd_findnext(&dj, &fno) == FR_OK && cnt != 0)
    cnt--; // skip cnt files
  f_closedir(&dj);
  if (cnt != 0)
    return;

  // Delete file if in delete mode
  if (browser_mode & BROWSER_DELETE) {
    f_unlink(fno.fname);
    return;
  }

  // Load file, get load function
  file_load_cb_t load = file_opt[browser_format].load;
  if (load == NULL)
    return;
  //
  lcd_set_colors(LCD_FG_COLOR, LCD_BG_COLOR);

  FIL* const file = filesystem_file();
  if (f_open(file, fno.fname, FA_READ) != FR_OK)
    return;
  //  START_PROFILE;
  const char* error = load(file, &fno, browser_format);
  f_close(file);
  //  STOP_PROFILE;
  // Check, need continue load next or previous file
  bool need_continue = file_opt[browser_format].opt & FILE_OPT_CONTINUE;
  if (error) {
    lcd_clear_screen();
    ui_message_box(error, fno.fname, need_continue ? 100 : 2000);
  }
  if (!need_continue)
    return;

  // Process input
  while (1) {
    uint16_t status = ui_input_check();
    int key = -1;
    if (status & EVT_DOWN)
      key = 0;
    if (status & EVT_UP)
      key = 1;
    if (status & EVT_BUTTON_SINGLE_CLICK)
      key = 2;

    status = touch_check();
    if (status == EVT_TOUCH_PRESSED || status == EVT_TOUCH_DOWN) {
      int touch_x, touch_y;
      touch_position(&touch_x, &touch_y);
      if (touch_x < LCD_WIDTH * 1 / 3)
        key = 0;
      else if (touch_x < LCD_WIDTH * 2 / 3)
        key = 2;
      else
        key = 1;
      touch_wait_release();
    }
    // chThdSleepMilliseconds(100); // Device hang after ~2min in this place, not switch thread back
    delay_milliseconds(100);
    int old_sel = sel;
    if (key == 0) {
      if (--sel < 0)
        sel = file_count - 1;
    } else if (key == 1) {
      if (++sel > file_count - 1)
        sel = 0;
    } else if (key == 2)
      break;
    if (old_sel != sel)
      goto repeat;
  }
}

static void browser_draw_buttons(void) {
  browser_draw_button(FILE_BUTTON_DEL, "DEL");
  browser_draw_button(FILE_BUTTON_LEFT, "<");
  browser_draw_button(FILE_BUTTON_RIGHT, ">");
  browser_draw_button(FILE_BUTTON_EXIT, "X");
}

static void browser_draw_page(int page) {
  FILINFO fno;
  DIR dj;
  // Mount SD card and open directory
  if (f_mount(filesystem_volume(), "", 1) != FR_OK ||
      sd_open_dir(&dj, "", file_opt[browser_format].ext) != FR_OK) {
    ui_message_box("ERROR", "NO CARD", 2000);
    ui_mode_normal();
    return;
  }
  // Draw Browser UI
  int cnt = 0;
  uint16_t start_file = (page - 1) * FILES_PER_PAGE;
  lcd_set_background(LCD_MENU_COLOR);
  // lcd_clear_screen();
  while (sd_findnext(&dj, &fno) == FR_OK) {
    if (cnt >= start_file && cnt < (start_file + FILES_PER_PAGE)) {
      // uint16_t sec = ((fno.ftime<<1)  & 0x3F);
      // uint16_t min = ((fno.ftime>>5)  & 0x3F);
      // uint16_t h   = ((fno.ftime>>11) & 0x1F);
      // uint16_t d   = ((fno.fdate>>0)  & 0x1F);
      // uint16_t m   = ((fno.fdate>>5)  & 0x0F);
      // uint16_t year= ((fno.fdate>>9)  & 0x3F) + 1980;
      // lcd_printf(x, y, "%2d %s %u - %u/%02u/%02u %02u:%02u:%02u", cnt, fno.fname, fno.fsize,
      // year, m, d, h, min, sec);
      browser_draw_button(cnt - start_file + FILE_BUTTON_FILE, fno.fname);
    }
    cnt++;
    if (file_count && (start_file + FILES_PER_PAGE == cnt))
      break;
  }
  f_closedir(&dj);
  // Calculate page and file count on first run
  if (file_count == 0) {
    file_count = cnt;
    page_count = cnt == 0 ? 1 : (file_count + FILES_PER_PAGE - 1) / FILES_PER_PAGE;
  }
  // Erase not used button
  cnt -= start_file;
  while (cnt < FILES_PER_PAGE) {
    browser_btn_t btn;
    browser_get_button_pos(cnt + FILE_BUTTON_FILE, &btn);
    lcd_fill(btn.x, btn.y, btn.w, btn.h);
    cnt++;
  }
  lcd_fill(0, LCD_HEIGHT - FILE_BOTTOM_HEIGHT, LCD_WIDTH, FILE_BOTTOM_HEIGHT);

  browser_draw_buttons();
  lcd_printf((LCD_WIDTH - FONT_STR_WIDTH(6)) / 2,
             LCD_HEIGHT - (FILE_BOTTOM_HEIGHT + FONT_STR_HEIGHT) / 2, "- %u | %u -", page,
             page_count);
  return;
}

static void browser_key_press(int key) {
  int page;
  switch (key) {
  case FILE_BUTTON_LEFT:
  case FILE_BUTTON_RIGHT: // Switch page on left / right change
    page = current_page;
    if (key == FILE_BUTTON_LEFT && --current_page < 1)
      current_page = page_count;
    if (key == FILE_BUTTON_RIGHT && ++current_page > page_count)
      current_page = 1;
    if (page != current_page)
      browser_draw_page(current_page);
    break;
  case FILE_BUTTON_EXIT: // Exit
    ui_mode_normal();
    break;
  case FILE_BUTTON_DEL: // Toggle delete mode
    browser_mode ^= BROWSER_DELETE;
    browser_draw_buttons();
    break;
  case FILE_BUTTON_FILE: // Open or delete file
  default:
    browser_open_file(key - FILE_BUTTON_FILE + (current_page - 1) * FILES_PER_PAGE);
    if (browser_mode & BROWSER_DELETE) {
      file_count = 0;                  // Reset file count (recalculate on draw page)
      browser_selection = -1;          // Reset selection
      browser_mode &= ~BROWSER_DELETE; // Exit file delete mode
      browser_draw_page(current_page);
      return;
    }
    ui_mode_normal(); // Exit
    break;
  }
}

static int browser_get_max(void) {
  // get max buttons depend from page and file count
  int max = current_page == page_count ? (file_count % FILES_PER_PAGE) : FILES_PER_PAGE;
  if (file_count > 0 && max == 0)
    max = FILES_PER_PAGE;
  return max + FILE_BUTTON_FILE - 1;
}

void sd_browser_enter(uint16_t format) {
  browser_format = format;
  current_page = 1;
  file_count = 0;
  browser_selection = -1;
  browser_mode = 0;
  browser_draw_page(current_page);
}

// Process UI input for browser
void ui_browser_touch(int touch_x, int touch_y) {
  browser_btn_t btn;
  int old = browser_selection;
  int max = browser_get_max();
  for (int idx = 0; idx <= max; idx++) {
    browser_get_button_pos(idx, &btn);
    if (touch_x < btn.x || touch_x >= btn.x + btn.w || touch_y < btn.y || touch_y >= btn.y + btn.h)
      continue;
    // Found button under touch
    browser_draw_button(browser_selection = idx, NULL); // draw new selection
    browser_draw_button(old, NULL);                     // clear old
    touch_wait_release();
    browser_selection = -1;
    browser_draw_button(idx, NULL); // clear selection
    browser_key_press(idx);
    return;
  }
}

void ui_browser_lever(uint16_t status) {
  if (status == EVT_BUTTON_SINGLE_CLICK) {
    if (browser_selection >= 0)
      browser_key_press(browser_selection); // Process click
    return;
  }
  int max = browser_get_max();
  do {
    int old = browser_selection;
    if ((status & EVT_DOWN) && --browser_selection < 0)
      browser_selection = max;
    if ((status & EVT_UP) && ++browser_selection > max)
      browser_selection = 0;
    if (old != browser_selection) {
      browser_draw_button(old, NULL);            // clear old selection
    browser_draw_button(browser_selection, NULL); // draw new selection
    }
    chThdSleepMilliseconds(100);
  } while ((status = ui_input_wait_release()) != 0);
}

#endif
