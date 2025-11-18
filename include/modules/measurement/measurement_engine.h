/*
 * Measurement engine public API.
 *
 * Provides a clean interface between the application and the sweep subsystem.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "measurement/pipeline.h"
#include "services/event_bus.h"

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
