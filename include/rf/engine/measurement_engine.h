/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rf/pipeline/measurement_pipeline.h"
#include "infra/event/event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct measurement_engine measurement_engine_t;
typedef struct measurement_engine_port measurement_engine_port_t;

typedef struct measurement_engine_request {
  bool break_on_operation;
} measurement_engine_request_t;

typedef struct measurement_engine_result {
  uint16_t sweep_mask;
  bool completed;
} measurement_engine_result_t;

struct measurement_engine_port {
  void* context;
  bool (*can_start_sweep)(measurement_engine_port_t* port,
                          measurement_engine_request_t* request);
  void (*handle_result)(measurement_engine_port_t* port,
                        const measurement_engine_result_t* result);
  void (*service_loop)(measurement_engine_port_t* port);
};

struct measurement_engine {
  measurement_engine_port_t* port;
  event_bus_t* event_bus;
  measurement_pipeline_t pipeline;
};

void measurement_engine_init(measurement_engine_t* engine, measurement_engine_port_t* port,
                             event_bus_t* bus, const PlatformDrivers* drivers);
void measurement_engine_tick(measurement_engine_t* engine);

#ifdef __cplusplus
}
#endif
