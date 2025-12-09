#pragma once

#include "core/data_types.h"
#include "ui/ui_style.h"

extern alignas(4) float measured[2][SWEEP_POINTS_MAX][2];

extern const trace_info_t trace_info_list[MAX_TRACE_TYPE];
extern const marker_info_t marker_info_list[MS_END];

extern config_t config;
extern properties_t current_props;

extern  uint8_t sweep_mode;
extern const char* const info_about[];
extern volatile bool calibration_in_progress;

extern pixel_t foreground_color;
extern pixel_t background_color;

extern alignas(4) pixel_t spi_buffer[SPI_BUFFER_SIZE];

extern uint16_t lastsaveid;
