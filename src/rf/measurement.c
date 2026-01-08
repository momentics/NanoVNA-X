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

#include "rf/measurement.h"

#include "rf/sweep.h"

#include "ch.h"

static inline void measurement_engine_publish(measurement_engine_t* engine, event_bus_topic_t event,
                                              const uint16_t* mask) {
  if (engine->event_bus != NULL) {
    event_bus_publish(engine->event_bus, event, mask);
  }
}

static inline void measurement_engine_service_loop(measurement_engine_t* engine) {
  if (engine->port != NULL && engine->port->service_loop != NULL) {
    engine->port->service_loop(engine->port);
  }
}

void measurement_engine_init(measurement_engine_t* engine, measurement_engine_port_t* port,
                             event_bus_t* bus, const PlatformDrivers* drivers) {
  if (engine == NULL) {
    return;
  }
  engine->port = port;
  engine->event_bus = bus;
  measurement_pipeline_init(&engine->pipeline, drivers);
  sweep_service_init(bus);
}

void measurement_engine_tick(measurement_engine_t* engine) {
  if (engine == NULL) {
    chThdSleepMilliseconds(1);
    return;
  }

  measurement_engine_service_loop(engine);

  measurement_engine_request_t request = {.break_on_operation = true};
  bool should_sweep = false;
  if (engine->port != NULL && engine->port->can_start_sweep != NULL) {
    should_sweep = engine->port->can_start_sweep(engine->port, &request);
  }
  if (!should_sweep) {
    chThdSleepMilliseconds(1);
    return;
  }

  const uint16_t mask = measurement_pipeline_active_mask(&engine->pipeline);

  sweep_service_wait_for_copy_release();
  sweep_service_begin_measurement();
  measurement_engine_publish(engine, EVENT_SWEEP_STARTED, &mask);
  const bool completed =
      measurement_pipeline_execute(&engine->pipeline, request.break_on_operation, mask);
  sweep_service_end_measurement();
  if (completed) {
    sweep_service_increment_generation();
    measurement_engine_publish(engine, EVENT_SWEEP_COMPLETED, &mask);
  }

  if (engine->port != NULL && engine->port->handle_result != NULL) {
    measurement_engine_result_t result = {.sweep_mask = mask, .completed = completed};
    engine->port->handle_result(engine->port, &result);
  }
}
