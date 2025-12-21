#include "ui/controller/marker_logic.h"
#include "ui/core/ui_model.h"
#include "ui/display/traces.h"
#include "nanovna.h"

// Helper predicates
static bool _greater(int x, int y) {
  return x > y;
}
static bool _lesser(int x, int y) {
  return x < y;
}

void marker_logic_search(void) {
  int i, value;
  int found = 0;
  
  // These globals are currently extern'd in nanovna.h
  // Ideally should be moved to model, but for Phase 5 we use as is.
  if (current_trace == TRACE_INVALID || active_marker == MARKER_INVALID)
    return;

  // Select search index table
  trace_index_const_table_t index = trace_index_const_table(current_trace);
  
  // Select compare function (depend from config settings)
  bool (*compare)(int x, int y) = VNA_MODE(VNA_MODE_SEARCH) ? _lesser : _greater;
  
  int points = ui_model_get_sweep_points();
  
  for (i = 1, value = TRACE_Y(index, 0); i < points; i++) {
    if ((*compare)(value, TRACE_Y(index, i))) {
      value = TRACE_Y(index, i);
      found = i;
    }
  }
  ui_model_set_marker_index(active_marker, found);
}

void marker_logic_search_dir(int16_t from, int16_t dir) {
  int i, value;
  int found = -1;
  if (current_trace == TRACE_INVALID || active_marker == MARKER_INVALID)
    return;
    
  trace_index_const_table_t index = trace_index_const_table(current_trace);
  bool (*compare)(int x, int y) = VNA_MODE(VNA_MODE_SEARCH) ? _lesser : _greater;
  
  int points = ui_model_get_sweep_points();
  
  // Search next
  for (i = from + dir, value = TRACE_Y(index, from); i >= 0 && i < points; i += dir) {
    if ((*compare)(value, TRACE_Y(index, i)))
      break;
    value = TRACE_Y(index, i);
  }
  //
  for (; i >= 0 && i < points; i += dir) {
    if ((*compare)(TRACE_Y(index, i), value))
      break;
    value = TRACE_Y(index, i);
    found = i;
  }
  if (found < 0)
    return;
  ui_model_set_marker_index(active_marker, found);
}

int marker_logic_distance_to_index(int8_t t, uint16_t idx, int16_t x, int16_t y) {
  trace_index_const_table_t index = trace_index_const_table(t);
  x -= (int16_t)TRACE_X(index, idx);
  y -= (int16_t)TRACE_Y(index, idx);
  return x * x + y * y;
}

int marker_logic_search_nearest_index(int x, int y, int t) {
  int min_i = -1;
  int min_d = MARKER_PICKUP_DISTANCE * MARKER_PICKUP_DISTANCE;
  int i;
  int points = ui_model_get_sweep_points();
  
  for (i = 0; i < points; i++) {
    int d = marker_logic_distance_to_index(t, i, x, y);
    if (d >= min_d)
      continue;
    min_d = d;
    min_i = i;
  }
  return min_i;
}

void marker_logic_update_index(void) {
     // Re-implementation of update_marker_index logic if needed
     // or access existing logic.
     // For now, this functionality seems embedded in plot.c's update_marker_index
     // We will leave it for now or move it if strictly required.
     // User requirement was not to lose functionality.
}
