#include "nanovna.h"
#include "ui/menus/menu_main.h"
#include "ui/menus/menu_cal.h"
#include "ui/menus/menu_stimulus.h"
#include "ui/menus/menu_display.h"
#include "ui/menus/menu_measure.h"
#include "ui/menus/menu_system.h"
#include "ui/menus/menu_marker.h" // For completeness/future use
#include "ui/core/ui_menu_engine.h"

#include "ui/menus/menu_storage.h"

// Callback for Pause/Resume Sweep
static UI_FUNCTION_ADV_CALLBACK(menu_pause_acb) {
  (void)data;
  if (b) {
    b->p1.text = (sweep_mode & SWEEP_ENABLE) ? "PAUSE" : "RESUME";
    b->icon = (sweep_mode & SWEEP_ENABLE) ? BUTTON_ICON_NOCHECK : BUTTON_ICON_CHECK;
    return;
  }
  toggle_sweep();
}

// Root Menu
const menuitem_t menu_top[] = {
    {MT_SUBMENU, 0, "CAL", menu_cal_menu},
    {MT_SUBMENU, 0, "STIMULUS", menu_stimulus},
    {MT_SUBMENU, 0, "DISPLAY", menu_display},
    {MT_SUBMENU, 0, "MEASURE", menu_measure_tools},
#ifdef __USE_SD_CARD__
    {MT_SUBMENU, 0, "SD CARD", menu_sdcard},
#endif
    {MT_SUBMENU, 0, "SYSTEM", menu_system},
    {MT_ADV_CALLBACK, 0, "%s\nSWEEP", menu_pause_acb},
    {MT_NEXT, 0, NULL, NULL} // sentinel
};
