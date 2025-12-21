/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CMD_MEASURE_NONE = 0,
    CMD_MEASURE_START,
    CMD_MEASURE_STOP,
    CMD_MEASURE_SINGLE,
    CMD_MEASURE_UPDATE_CONFIG
} measurement_command_type_t;

typedef struct {
    measurement_command_type_t type;
    union {
        struct {
            bool oneshot;
        } start;
        struct {
            uint32_t flags; // Placeholder for config updates
        } update;
    } data;
} measurement_command_t;

// API to post commands to the measurement thread
void measurement_post_command(measurement_command_t cmd);
