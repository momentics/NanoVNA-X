#pragma once

#include "core/data_types.h"

extern properties_t current_props;
extern config_t config;

#define frequency0          current_props._frequency0
#define frequency1          current_props._frequency1
#define cal_frequency0      current_props._cal_frequency0
#define cal_frequency1      current_props._cal_frequency1
#define var_freq            current_props._var_freq
#define sweep_points        current_props._sweep_points
#define cal_sweep_points    current_props._cal_sweep_points
#define cal_power           current_props._cal_power
#define cal_status          current_props._cal_status
#define cal_data            current_props._cal_data
#define electrical_delayS11 current_props._electrical_delay[0]
#define electrical_delayS21 current_props._electrical_delay[1]
#define s21_offset          current_props._s21_offset
#define velocity_factor     current_props._velocity_factor
#define trace               current_props._trace
#define current_trace       current_props._current_trace
#define markers             current_props._markers
#define active_marker       current_props._active_marker
#define previous_marker     current_props._previous_marker
#ifdef __VNA_Z_RENORMALIZATION__
 #define cal_load_r         current_props._cal_load_r
#else
 #define cal_load_r         50.0f
#endif

#define props_mode          current_props._mode
#define domain_window      (props_mode&TD_WINDOW)
#define domain_func        (props_mode&TD_FUNC)
