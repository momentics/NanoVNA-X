/*
 * Measurement subsystem module
 *
 * Provides a thin wrapper around the existing sweep service helpers so the
 * application can treat the whole measurement stack as a single component.
 */

#include "app/modules/measurement.h"

#include "app/sweep_service.h"

void measurement_module_init(measurement_module_t* module, measurement_pipeline_t* pipeline) {
  if (module == NULL) {
    return;
  }
  module->pipeline = pipeline;
  module->port.pipeline = pipeline;
  static const measurement_port_api_t measurement_port_api = {
      .active_mask = measurement_pipeline_active_mask,
      .wait_for_copy_release = sweep_service_wait_for_copy_release,
      .begin_measurement = sweep_service_begin_measurement,
      .execute = measurement_pipeline_execute,
      .end_measurement = sweep_service_end_measurement,
      .increment_generation = sweep_service_increment_generation,
  };
  module->port.api = &measurement_port_api;
}

const measurement_port_t* measurement_module_port(measurement_module_t* module) {
  if (module == NULL) {
    return NULL;
  }
  return &module->port;
}
