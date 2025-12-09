#include "nanovna.h"
#include "ui/ui_internal.h"
#include "ui/menus/menu_internal.h"
#include "nanovna.h" // For plot_set_measure_mode which is in nanovna.h or ui_internal.h depending on codebase versions, but we saw it in nanovna.h

#ifdef __VNA_MEASURE_MODULE__

// Forward declaration if needed, or we define it below
extern const menuitem_t* const menu_measure_list[];

UI_FUNCTION_ADV_CALLBACK(menu_measure_acb) {
  if (b) {
    b->icon = current_props._measure == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    return;
  }
  plot_set_measure_mode(data);
  menu_set_submenu(menu_measure_list[current_props._measure]);
}

UI_FUNCTION_CALLBACK(menu_measure_cb) {
  (void)data;
  menu_push_submenu(menu_measure_list[current_props._measure]);
}

// Select menu depend from measure mode
#ifdef __USE_LC_MATCHING__
const menuitem_t menu_measure_lc[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_LC_MATH, "L/C MATCH", menu_measure_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

#ifdef __S11_CABLE_MEASURE__
const menuitem_t menu_measure_cable[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_S11_CABLE, "CABLE\n (S11)", menu_measure_acb},
    {MT_ADV_CALLBACK, KM_VELOCITY_FACTOR, "VELOCITY F.\n " R_LINK_COLOR "%d%%",
     menu_keyboard_acb},
    {MT_ADV_CALLBACK, KM_ACTUAL_CABLE_LEN, "CABLE LENGTH", menu_keyboard_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

#ifdef __S11_RESONANCE_MEASURE__
const menuitem_t menu_measure_resonance[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_S11_RESONANCE, "RESONANCE\n (S11)", menu_measure_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

#ifdef __S21_MEASURE__
const menuitem_t menu_measure_s21[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SHUNT_LC, "SHUNT LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_LC, "SERIES LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_XTAL, "SERIES\nXTAL (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, KM_MEASURE_R, " Rl = " R_LINK_COLOR "%b.4F" S_OHM, menu_keyboard_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

const menuitem_t menu_measure_filter[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_FILTER, "FILTER\n (S21)", menu_measure_acb},
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};
#endif

const menuitem_t menu_measure[] = {
    {MT_ADV_CALLBACK, MEASURE_NONE, "OFF", menu_measure_acb},
#ifdef __USE_LC_MATCHING__
    {MT_ADV_CALLBACK, MEASURE_LC_MATH, "L/C MATCH", menu_measure_acb},
#endif
#ifdef __S11_CABLE_MEASURE__
    {MT_ADV_CALLBACK, MEASURE_S11_CABLE, "CABLE\n (S11)", menu_measure_acb},
#endif
#ifdef __S11_RESONANCE_MEASURE__
    {MT_ADV_CALLBACK, MEASURE_S11_RESONANCE, "RESONANCE\n (S11)", menu_measure_acb},
#endif
#ifdef __S21_MEASURE__
    {MT_ADV_CALLBACK, MEASURE_SHUNT_LC, "SHUNT LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_LC, "SERIES LC\n (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_SERIES_XTAL, "SERIES\nXTAL (S21)", menu_measure_acb},
    {MT_ADV_CALLBACK, MEASURE_FILTER, "FILTER\n (S21)", menu_measure_acb},
#endif
    {MT_NEXT, 0, NULL, menu_back} // next-> menu_back
};

// Dynamic menu selector depend from measure mode
const menuitem_t* const menu_measure_list[] = {
    [MEASURE_NONE] = menu_measure,
#ifdef __USE_LC_MATCHING__
    [MEASURE_LC_MATH] = menu_measure_lc,
#endif
#ifdef __S21_MEASURE__
    [MEASURE_SHUNT_LC] = menu_measure_s21,
    [MEASURE_SERIES_LC] = menu_measure_s21,
    [MEASURE_SERIES_XTAL] = menu_measure_s21,
    [MEASURE_FILTER] = menu_measure_filter,
#endif
#ifdef __S11_CABLE_MEASURE__
    [MEASURE_S11_CABLE] = menu_measure_cable,
#endif
#ifdef __S11_RESONANCE_MEASURE__
    [MEASURE_S11_RESONANCE] = menu_measure_resonance,
#endif
};
#endif
#ifdef BANDWIDTH_8000
static const menu_descriptor_t menu_bandwidth_desc[] = {
#ifdef BANDWIDTH_8000
    {MT_ADV_CALLBACK, BANDWIDTH_8000},
#endif
#ifdef BANDWIDTH_4000
    {MT_ADV_CALLBACK, BANDWIDTH_4000},
#endif
#ifdef BANDWIDTH_2000
    {MT_ADV_CALLBACK, BANDWIDTH_2000},
#endif
#ifdef BANDWIDTH_1000
    {MT_ADV_CALLBACK, BANDWIDTH_1000},
#endif
#ifdef BANDWIDTH_333
    {MT_ADV_CALLBACK, BANDWIDTH_333},
#endif
#ifdef BANDWIDTH_100
    {MT_ADV_CALLBACK, BANDWIDTH_100},
#endif
#ifdef BANDWIDTH_30
    {MT_ADV_CALLBACK, BANDWIDTH_30},
#endif
#ifdef BANDWIDTH_10
    {MT_ADV_CALLBACK, BANDWIDTH_10},
#endif
};
#endif

UI_FUNCTION_ADV_CALLBACK(menu_bandwidth_acb) {
  if (b) {
    b->icon = config._bandwidth == data ? BUTTON_ICON_GROUP_CHECKED : BUTTON_ICON_GROUP;
    b->p1.u = get_bandwidth_frequency(data);
    return;
  }
  set_bandwidth(data);
}

// Explicit prototype if header fail
menuitem_t* menu_dynamic_acquire(void);

const menuitem_t* menu_build_bandwidth_menu(void) {
  menuitem_t* buffer = menu_dynamic_acquire();
  menuitem_t* cursor = buffer;
#ifdef BANDWIDTH_8000
  cursor = ui_menu_list(menu_bandwidth_desc, ARRAY_COUNT(menu_bandwidth_desc), "%u " S_Hz,
                        menu_bandwidth_acb, cursor);
#endif
  menu_set_next(cursor, menu_back);
  return buffer;
}

UI_FUNCTION_ADV_CALLBACK(menu_bandwidth_sel_acb) {
  (void)data;
  if (b) {
    b->p1.u = get_bandwidth_frequency(config._bandwidth);
    return;
  }
  menu_push_submenu(menu_build_bandwidth_menu());
}
