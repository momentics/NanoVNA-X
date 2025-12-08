#include "ui/menus/menu_internal.h"

//*******************************************************************************
// Stimulus Menu
//*******************************************************************************

UI_FUNCTION_ADV_CALLBACK(menu_points_sel_acb) {
  (void)data;
  if (b) {
    b->p1.u = sweep_points;
    return;
  }
  menu_push_submenu(menu_build_points_menu());
}

static const uint16_t point_counts_set[POINTS_SET_COUNT] = POINTS_SET;
UI_FUNCTION_ADV_CALLBACK(menu_points_acb) {
  uint16_t p_count = point_counts_set[data];
  if (b) {
    b->icon = sweep_points == p_count ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = p_count;
    return;
  }
  set_sweep_points(p_count);
}

static const menu_descriptor_t menu_points_desc[] = {
    {MT_ADV_CALLBACK, 0}, {MT_ADV_CALLBACK, 1},
    {MT_ADV_CALLBACK, 2}, {MT_ADV_CALLBACK, 3},
    {MT_ADV_CALLBACK, 4},
#if POINTS_SET_COUNT > 5
    {MT_ADV_CALLBACK, 5},
#endif
#if POINTS_SET_COUNT > 6
    {MT_ADV_CALLBACK, 6},
#endif
#if POINTS_SET_COUNT > 7
    {MT_ADV_CALLBACK, 7},
#endif
#if POINTS_SET_COUNT > 8
    {MT_ADV_CALLBACK, 8},
#endif
};

const menuitem_t* menu_build_points_menu(void) {
  menuitem_t* cursor = menu_dynamic_acquire();
  *cursor++ = (menuitem_t){MT_ADV_CALLBACK, KM_POINTS, "SET POINTS\n " R_LINK_COLOR "%d",
                           (const void*)menu_keyboard_acb};
  
  cursor = ui_menu_list(menu_points_desc, ARRAY_COUNT(menu_points_desc), "%d point",
                        menu_points_acb, cursor);
  menu_set_next(cursor, menu_back);
  return menu_dynamic_acquire();
}

const menuitem_t menu_stimulus[] = {
    {MT_ADV_CALLBACK, KM_START, "START", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_STOP, "STOP", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_CENTER, "CENTER", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_SPAN, "SPAN", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_CW, "CW FREQ", menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_STEP, "FREQ STEP\n " R_LINK_COLOR "%bF" S_Hz, menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_VAR, "JOG STEP\n " R_LINK_COLOR "AUTO", menu_keyboard_acb},
    {MT_ADV_CALLBACK, 0, "MORE PTS\n " R_LINK_COLOR "%u", menu_points_sel_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
