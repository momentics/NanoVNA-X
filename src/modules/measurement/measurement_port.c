#include "modules/measurement/measurement_port.h"

static void measurement_port_pipeline_init(measurement_module_context_t* context,
                                           const PlatformDrivers* drivers) {
  if (context == NULL) {
    return;
  }
  measurement_pipeline_init(&context->pipeline, drivers);
}

static uint16_t measurement_port_active_mask(measurement_module_context_t* context) {
  if (context == NULL) {
    return 0;
  }
  return measurement_pipeline_active_mask(&context->pipeline);
}

static bool measurement_port_execute(measurement_module_context_t* context, bool break_on_operation,
                                     uint16_t channel_mask) {
  if (context == NULL) {
    return false;
  }
  return measurement_pipeline_execute(&context->pipeline, break_on_operation, channel_mask);
}

const measurement_port_api_t measurement_port_api = {
    .pipeline_init = measurement_port_pipeline_init,
    .active_mask = measurement_port_active_mask,
    .execute = measurement_port_execute,
    .service_init = sweep_service_init,
    .wait_for_copy_release = sweep_service_wait_for_copy_release,
    .begin_measurement = sweep_service_begin_measurement,
    .end_measurement = sweep_service_end_measurement,
    .increment_generation = sweep_service_increment_generation,
    .wait_for_generation = sweep_service_wait_for_generation,
    .reset_progress = sweep_service_reset_progress,
    .snapshot_acquire = sweep_service_snapshot_acquire,
    .snapshot_release = sweep_service_snapshot_release,
    .start_capture = sweep_service_start_capture,
    .wait_for_capture = sweep_service_wait_for_capture,
    .rx_buffer = sweep_service_rx_buffer,
#if ENABLED_DUMP_COMMAND
    .prepare_dump = sweep_service_prepare_dump,
    .dump_ready = sweep_service_dump_ready,
#endif
    .sweep_mask = app_measurement_get_sweep_mask,
    .sweep = app_measurement_sweep,
    .set_frequency = app_measurement_set_frequency,
    .set_frequencies = app_measurement_set_frequencies,
    .update_frequencies = app_measurement_update_frequencies,
    .transform_domain = app_measurement_transform_domain,
    .set_sample_function = sweep_service_set_sample_function,
    .set_smooth_factor = set_smooth_factor,
    .get_smooth_factor = get_smooth_factor};
