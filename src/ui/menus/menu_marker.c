#include "ch.h"
#include "hal.h"
#include "nanovna.h"
#include "ui/ui_menu.h"
#include "ui/core/ui_core.h"
#include "ui/core/ui_menu_engine.h"
#include "ui/menus/menu_marker.h"
#include "ui/menus/menu_display.h"

// Macros
#define UI_MARKER_EDELAY 6

// Helpers
static void active_marker_check(void) {
  int i;
  // Auto select active marker if disabled
  if (active_marker == MARKER_INVALID)
    for (i = 0; i < MARKERS_MAX; i++)
      if (markers[i].enabled)
        active_marker = i;
  // Auto select previous marker if disabled
  if (previous_marker == active_marker)
    previous_marker = MARKER_INVALID;
  if (previous_marker == MARKER_INVALID) {
    for (i = 0; i < MARKERS_MAX; i++)
      if (markers[i].enabled && i != active_marker)
        previous_marker = i;
  }
}

// Callbacks

static UI_FUNCTION_CALLBACK(menu_marker_op_cb) {
  freq_t freq = get_marker_frequency(active_marker);
  if (freq == 0)
    return; // no active marker
  switch (data) {
  case ST_START:
  case ST_STOP:
  case ST_CENTER:
    set_sweep_frequency(data, freq);
    break;
  case ST_SPAN:
    if (previous_marker == MARKER_INVALID || active_marker == previous_marker) {
      // if only 1 marker is active, keep center freq and make span the marker comes to the edge
      freq_t center = get_sweep_frequency(ST_CENTER);
      freq_t span = center > freq ? center - freq : freq - center;
      set_sweep_frequency(ST_SPAN, span * 2);
    } else {
      // if 2 or more marker active, set start and stop freq to each marker
      freq_t freq2 = get_marker_frequency(previous_marker);
      if (freq2 == 0)
        return;
      if (freq > freq2)
        SWAP(freq_t, freq2, freq);
      set_sweep_frequency(ST_START, freq);
      set_sweep_frequency(ST_STOP, freq2);
    }
    break;
  case UI_MARKER_EDELAY:
    if (current_trace != TRACE_INVALID) {
      int ch = trace[current_trace].channel;
      float(*array)[2] = measured[ch];
      int index = markers[active_marker].index;
      float v = groupdelay_from_array(index, array[index]);
      set_electrical_delay(ch, current_props._electrical_delay[ch] + v);
    }
    break;
  }
  ui_mode_normal();
}

static UI_FUNCTION_CALLBACK(menu_marker_search_dir_cb) {
  marker_search_dir(markers[active_marker].index,
                    data == MK_SEARCH_RIGHT ? MK_SEARCH_RIGHT : MK_SEARCH_LEFT);
  props_mode &= ~TD_MARKER_TRACK;
#ifdef UI_USE_LEVELER_SEARCH_MODE
  select_lever_mode(LM_SEARCH);
#endif
}

static UI_FUNCTION_ADV_CALLBACK(menu_marker_tracking_acb) {
  (void)data;
  if (b) {
    b->icon = (props_mode & TD_MARKER_TRACK) ? BUTTON_ICON_CHECK : BUTTON_ICON_NOCHECK;
    return;
  }
  props_mode ^= TD_MARKER_TRACK;
}

static UI_FUNCTION_ADV_CALLBACK(menu_marker_sel_acb) {
  // if (data >= MARKERS_MAX) return;
  int mk = data;
  if (b) {
    if (mk == active_marker) {
      b->icon = BUTTON_ICON_CHECK_AUTO;
    } else if (markers[mk].enabled) {
      b->icon = BUTTON_ICON_CHECK;
}
    b->p1.u = mk + 1;
    return;
  }
  // Marker select click
  if (markers[mk].enabled) {          // Marker enabled
    if (mk == active_marker) {        // If active marker:
      markers[mk].enabled = FALSE;    //  disable it
      mk = previous_marker;           //  set select from previous marker
      active_marker = MARKER_INVALID; //  invalidate active
      request_to_redraw(REDRAW_AREA);
    }
  } else {
    markers[mk].enabled = TRUE; // Enable marker
  }
  previous_marker = active_marker; // set previous marker as current active
  active_marker = mk;              // set new active marker
  active_marker_check();
  request_to_redraw(REDRAW_MARKER);
}

static UI_FUNCTION_CALLBACK(menu_marker_disable_all_cb) {
  (void)data;
  for (int i = 0; i < MARKERS_MAX; i++)
    markers[i].enabled = FALSE; // all off
  previous_marker = MARKER_INVALID;
  active_marker = MARKER_INVALID;
  request_to_redraw(REDRAW_AREA);
}

static UI_FUNCTION_ADV_CALLBACK(menu_marker_delta_acb) {
  (void)data;
  if (b) {
    b->icon = props_mode & TD_MARKER_DELTA ? BUTTON_ICON_CHECK : BUTTON_ICON_NOCHECK;
    return;
  }
  props_mode ^= TD_MARKER_DELTA;
  request_to_redraw(REDRAW_MARKER);
}

// Descriptors and Dynamic Builders

static const menu_descriptor_t MENU_MARKER_SEL_DESC[] = {
  {MT_ADV_CALLBACK, 0},
#if MARKERS_MAX >= 2
  {MT_ADV_CALLBACK, 1},
#endif
#if MARKERS_MAX >= 3
  {MT_ADV_CALLBACK, 2},
#endif
#if MARKERS_MAX >= 4
  {MT_ADV_CALLBACK, 3},
#endif
#if MARKERS_MAX >= 5
  {MT_ADV_CALLBACK, 4},
#endif
#if MARKERS_MAX >= 6
  {MT_ADV_CALLBACK, 5},
#endif
#if MARKERS_MAX >= 7
  {MT_ADV_CALLBACK, 6},
#endif
#if MARKERS_MAX >= 8
  {MT_ADV_CALLBACK, 7},
#endif
};

const menuitem_t *menu_build_marker_select_menu(void) {
  menuitem_t *cursor = menu_dynamic_acquire();
  const menuitem_t *base = cursor;
  cursor = ui_menu_list(MENU_MARKER_SEL_DESC, ARRAY_COUNT(MENU_MARKER_SEL_DESC), "MARKER %d",
                        menu_marker_sel_acb, cursor);
  *cursor++ = (menuitem_t){MT_CALLBACK, 0, "ALL OFF", menu_marker_disable_all_cb};
  *cursor++ = (menuitem_t){MT_ADV_CALLBACK, 0, "DELTA", menu_marker_delta_acb};
  menu_set_next(cursor, MENU_BACK);
  return base;
}

static UI_FUNCTION_CALLBACK(menu_marker_select_cb) {
  (void)data;
  menu_push_submenu(menu_build_marker_select_menu());
}

const menuitem_t MENU_MARKER[] = {
  {MT_CALLBACK, 0, "SELECT\nMARKER", menu_marker_select_cb},
  {MT_ADV_CALLBACK, 0, "TRACKING", menu_marker_tracking_acb},
  {MT_ADV_CALLBACK, VNA_MODE_SEARCH, "SEARCH\n " R_LINK_COLOR "%s", menu_vna_mode_acb},
  {MT_CALLBACK, MK_SEARCH_LEFT, "SEARCH\n " S_LARROW "LEFT", menu_marker_search_dir_cb},
  {MT_CALLBACK, MK_SEARCH_RIGHT, "SEARCH\n " S_RARROW "RIGHT", menu_marker_search_dir_cb},
  {MT_CALLBACK, ST_START, "MOVE\nSTART", menu_marker_op_cb},
  {MT_CALLBACK, ST_STOP, "MOVE\nSTOP", menu_marker_op_cb},
  {MT_CALLBACK, ST_CENTER, "MOVE\nCENTER", menu_marker_op_cb},
  {MT_CALLBACK, ST_SPAN, "MOVE\nSPAN", menu_marker_op_cb},
  {MT_CALLBACK, UI_MARKER_EDELAY, "MARKER\nE-DELAY", menu_marker_op_cb},
  {MT_ADV_CALLBACK, 0, "DELTA", menu_marker_delta_acb},
  {MT_NEXT, 0, NULL, MENU_BACK} // next-> MENU_BACK
};
