#include "ui/core/ui_model.h"
#include <string.h>

void ui_model_init(void) {
    // Initialize model systems if needed
}

// Trace Access
trace_t* ui_model_get_trace(int index) {
    if (index < 0 || index >= TRACES_MAX) return NULL;
    return &trace[index];
}

bool ui_model_trace_enabled(int index) {
    if (index < 0 || index >= TRACES_MAX) return false;
    return trace[index].enabled;
}

uint8_t ui_model_get_trace_type(int index) {
     if (index < 0 || index >= TRACES_MAX) return 0; // Default or Error
     return trace[index].type;
}

uint8_t ui_model_get_trace_channel(int index) {
    if (index < 0 || index >= TRACES_MAX) return 0;
    return trace[index].channel;
}

// Marker Access
marker_t* ui_model_get_marker(int index) {
    if (index < 0 || index >= MARKERS_MAX) return NULL;
    return &markers[index];
}

freq_t ui_model_get_marker_frequency(int index) {
    if (index < 0 || index >= MARKERS_MAX) return 0;
    return markers[index].frequency;
}

void ui_model_set_marker_frequency(int index, freq_t freq) {
    if (index < 0 || index >= MARKERS_MAX) return;
    markers[index].frequency = freq;
}

uint16_t ui_model_get_marker_index(int index) {
    if (index < 0 || index >= MARKERS_MAX) return 0;
    return markers[index].index;
}

void ui_model_set_marker_index(int index, uint16_t idx) {
    if (index < 0 || index >= MARKERS_MAX) return;
    markers[index].index = idx;
}

// Global State
freq_t ui_model_get_sweep_frequency(int type) {
    return get_sweep_frequency(type);
}

void ui_model_set_sweep_frequency(int type, freq_t freq) {
    set_sweep_frequency(type, freq);
}

uint16_t ui_model_get_sweep_points(void) {
    return sweep_points;
}

float ui_model_get_electrical_delay(void) {
    return get_electrical_delay();
}

// Calibration Cache
void ui_model_get_touch_cal(int16_t* dest) {
    if (dest) memcpy(dest, config._touch_cal, sizeof(config._touch_cal));
}

bool ui_model_touch_cal_changed(const int16_t* cache) {
     return memcmp(cache, config._touch_cal, 4 * sizeof(int16_t)) != 0;
}

void ui_model_update_touch_cal(int16_t* cache) {
    if (cache) memcpy(cache, config._touch_cal, 4 * sizeof(int16_t));
}
