#include <string.h>
#include "hal.h"

#include "ui/menus/menu_internal.h"
#include "ui/controller/vna_browser.h"
#include "ui/input/ui_keypad.h"
#include "ui/input/ui_touch.h"

// Dependencies from ui_controller.c or other modules
// keyboard_temp is in ui_internal.h (extern)

// ----------------------------------------------------------------------------
// Helper: Fix screenshot format
static uint16_t fix_screenshot_format(uint16_t data) {
#ifdef __SD_CARD_DUMP_TIFF__
  if (data == FMT_BMP_FILE && VNA_MODE(VNA_MODE_TIFF))
    return FMT_TIF_FILE;
#endif
  return data;
}

// ----------------------------------------------------------------------------
// Callbacks

UI_FUNCTION_ADV_CALLBACK(menu_save_acb) {
  if (b) {
    const properties_t* p = get_properties(data);
    if (p)
      plot_printf(b->label, sizeof(b->label), "%.6F" S_Hz "\n%.6F" S_Hz, (float)p->_frequency0,
                  (float)p->_frequency1);
    else
      b->p1.u = data;
    return;
  }
  
  // Check if calibration is in progress before attempting to save
  if (calibration_in_progress) {
    ui_message_box("BUSY", "Calibration in progress, try again later", 2000);
    return;
  }
  
  int result = caldata_save(data);
  
  if (result == 0) {
    menu_move_back(true);
    request_to_redraw(REDRAW_BACKUP | REDRAW_CAL_STATUS);
  } else {
    // Show error if save failed
    ui_message_box("SAVE ERROR", "Failed to save calibration", 1000);
  }
}

UI_FUNCTION_CALLBACK(menu_save_submenu_cb) {
  (void)data;
  menu_push_submenu(menu_build_save_menu());
}

UI_FUNCTION_ADV_CALLBACK(menu_recall_acb) {
  if (b) {
    const properties_t* p = get_properties(data);
    if (p)
      plot_printf(b->label, sizeof(b->label), "%.6F" S_Hz "\n%.6F" S_Hz, (float)p->_frequency0,
                  (float)p->_frequency1);
    else
      b->p1.u = data;
    if (lastsaveid == data)
      b->icon = BUTTON_ICON_CHECK;
    return;
  }
  
  load_properties(data);
}

UI_FUNCTION_CALLBACK(menu_recall_submenu_cb) {
  (void)data;
  menu_push_submenu(menu_build_recall_menu());
}

#ifdef __USE_SD_CARD__

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

UI_FUNCTION_CALLBACK(menu_sdcard_format_cb) {
  (void)data;
  bool resume = (sweep_mode & SWEEP_ENABLE) != 0;
  if (resume)
    toggle_sweep();
  systime_t start = chVTGetSystemTimeX();
  // ui_message_box_draw is not exposed, use ui_message_box with delay 0?
  // Or just ui_message_box("FORMAT SD", "Formatting...", 0);
  // ui_message_box calls lcd logic and waits if delay > 0.
  // Actually ui_message_box with 0 delay might block? No.
  ui_message_box("FORMAT SD", "Formatting...", 0);
  // We need to sleep to let user see it? 
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

#ifdef __SD_FILE_BROWSER__

UI_FUNCTION_CALLBACK(menu_sdcard_browse_cb) {
  data = fix_screenshot_format(data);
  ui_mode_browser(data);
}

static const menuitem_t menu_sdcard_browse[] = {
    {MT_CALLBACK, FMT_BMP_FILE, "LOAD\nSCREENSHOT", menu_sdcard_browse_cb},
    {MT_CALLBACK, FMT_S1P_FILE, "LOAD S1P", menu_sdcard_browse_cb},
    {MT_CALLBACK, FMT_S2P_FILE, "LOAD S2P", menu_sdcard_browse_cb},
    {MT_CALLBACK, FMT_CAL_FILE, "LOAD CAL", menu_sdcard_browse_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

#endif // __SD_FILE_BROWSER__

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


static const menu_descriptor_t menu_state_slots_desc[] = {
    {MT_ADV_CALLBACK, 0},
    {MT_ADV_CALLBACK, 1},
    {MT_ADV_CALLBACK, 2},
#if SAVEAREA_MAX > 3
    {MT_ADV_CALLBACK, 3},
#endif
#if SAVEAREA_MAX > 4
    {MT_ADV_CALLBACK, 4},
#endif
#if SAVEAREA_MAX > 5
    {MT_ADV_CALLBACK, 5},
#endif
#if SAVEAREA_MAX > 6
    {MT_ADV_CALLBACK, 6},
#endif
};

#if defined(__SD_FILE_BROWSER__)
#define MENU_STATE_SD_ENTRY 1
#else
#define MENU_STATE_SD_ENTRY 0
#endif

// Exposed in menu_internal.h
const menuitem_t menu_state_io[] = {
    {MT_CALLBACK, 0, "SAVE CAL", menu_save_submenu_cb},
    {MT_CALLBACK, 0, "RECALL CAL", menu_recall_submenu_cb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t* menu_build_save_menu(void) {
  menuitem_t* buffer = menu_dynamic_acquire();
  menuitem_t* cursor = buffer;
#ifdef __SD_FILE_BROWSER__
  *cursor++ =
      (menuitem_t){MT_CALLBACK, FMT_CAL_FILE, "SAVE TO\n SD CARD", menu_sdcard_cb};
#endif
  cursor = ui_menu_list(menu_state_slots_desc, ARRAY_COUNT(menu_state_slots_desc), "Empty %d",
                        menu_save_acb, cursor);
  menu_set_next(cursor, menu_back);
  return buffer;
}

const menuitem_t* menu_build_recall_menu(void) {
  menuitem_t* buffer = menu_dynamic_acquire();
  menuitem_t* cursor = buffer;
#ifdef __SD_FILE_BROWSER__
  *cursor++ = (menuitem_t){MT_CALLBACK, FMT_CAL_FILE, "LOAD FROM\n SD CARD",
                           menu_sdcard_browse_cb};
#endif
  cursor = ui_menu_list(menu_state_slots_desc, ARRAY_COUNT(menu_state_slots_desc), "Empty %d",
                        menu_recall_acb, cursor);
  menu_set_next(cursor, menu_back);
  return buffer;
}
