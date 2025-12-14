#ifndef UI_MENUS_MENU_STORAGE_H
#define UI_MENUS_MENU_STORAGE_H

#include "ui/ui_menu.h"

// Expose the SD Card menu for usage in menu_main.c
extern const menuitem_t MENU_SDCARD[];

#ifdef SD_FILE_BROWSER
extern const menuitem_t menu_sdcard_browse[];
#endif

void input_filename(uint16_t data, button_t *b);

#endif // UI_MENUS_MENU_STORAGE_H
