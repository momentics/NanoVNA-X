/*
 * Measurement engine: orchestrates sweep execution and DSP post-processing.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/sweep_service.h"
#include "measurement/pipeline.h"
#include "services/event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool completed;
  uint16_t active_channels;
} measurement_cycle_result_t;

typedef struct measurement_engine {
  measurement_pipeline_t pipeline;
  event_bus_t* bus;
  measurement_cycle_result_t last_cycle;
} measurement_engine_t;

void measurement_engine_init(measurement_engine_t* engine,
                             const PlatformDrivers* drivers,
                             event_bus_t* bus);
const measurement_cycle_result_t* measurement_engine_execute(measurement_engine_t* engine);

#ifdef __cplusplus
}
#endif
