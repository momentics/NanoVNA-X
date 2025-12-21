#ifndef UI_MODEL_H
#define UI_MODEL_H

#include "nanovna.h"
#include <stdint.h>
#include <stdbool.h>

// Initialize Model (e.g. buffers)
void ui_model_init(void);

// Trace Access
trace_t* ui_model_get_trace(int index);
bool ui_model_trace_enabled(int index);
uint8_t ui_model_get_trace_type(int index);
uint8_t ui_model_get_trace_channel(int index);

// Marker Access
marker_t* ui_model_get_marker(int index);
freq_t ui_model_get_marker_frequency(int index);
void ui_model_set_marker_frequency(int index, freq_t freq);
uint16_t ui_model_get_marker_index(int index);
void ui_model_set_marker_index(int index, uint16_t idx);

// Global State
freq_t ui_model_get_sweep_frequency(int type);
void ui_model_set_sweep_frequency(int type, freq_t freq);
uint16_t ui_model_get_sweep_points(void);
float ui_model_get_electrical_delay(void);

// Calibration Cache (moved from ui_core)
void ui_model_get_touch_cal(int16_t* dest);
bool ui_model_touch_cal_changed(const int16_t* cache);
void ui_model_update_touch_cal(int16_t* cache);

#endif // UI_MODEL_H
