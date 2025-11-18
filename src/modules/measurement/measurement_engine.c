#include "modules/measurement/measurement_engine.h"

#include "app/sweep_service.h"

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
  sweep_service_init();
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
