#ifndef _UI_MENUS_MENU_STORAGE_H_
#define _UI_MENUS_MENU_STORAGE_H_

#include "ui/ui_menu.h"

// Expose the SD Card menu for usage in menu_main.c
extern const menuitem_t MENU_SDCARD[];

#ifdef __SD_FILE_BROWSER__
extern const menuitem_t menu_sdcard_browse[];
#endif

void input_filename(uint16_t data, button_t *b);

#endif // _UI_MENUS_MENU_STORAGE_H_
