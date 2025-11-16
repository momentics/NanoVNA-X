#include "app/modules/measurement_engine.h"

#include "app/sweep_service.h"
#include "measurement/pipeline.h"
#include "services/event_bus.h"
#include "ui/ui_internal.h"

#include "ch.h"

static void publish_event(event_bus_t* bus, event_bus_topic_t topic, uint16_t* mask) {
  if (bus != NULL) {
    event_bus_publish(bus, topic, mask);
  }
}

void measurement_engine_init(measurement_engine_t* engine,
                             const measurement_engine_config_t* config) {
  chDbgAssert(engine != NULL, "measurement_engine_init: engine");
  chDbgAssert(config != NULL, "measurement_engine_init: config");
  engine->config = *config;
  engine->last_cycle.completed = false;
  engine->last_cycle.active_channels = 0;
  measurement_pipeline_init(&engine->pipeline, config->drivers);
  sweep_service_init();
}

const measurement_cycle_result_t* measurement_engine_execute(measurement_engine_t* engine,
                                                             bool allow_break) {
  chDbgAssert(engine != NULL, "measurement_engine_execute: engine");
  engine->last_cycle.completed = false;
  uint16_t mask = measurement_pipeline_active_mask(&engine->pipeline);
  engine->last_cycle.active_channels = mask;
  bool sweep_allowed = true;
  if (engine->config.port.is_sweep_enabled != NULL) {
    sweep_allowed = engine->config.port.is_sweep_enabled(engine->config.port.context);
  }
  if (sweep_allowed) {
    sweep_service_wait_for_copy_release();
    sweep_service_begin_measurement();
    publish_event(engine->config.event_bus, EVENT_SWEEP_STARTED, &mask);
    if (engine->config.port.on_cycle_started != NULL) {
      engine->config.port.on_cycle_started(engine->config.port.context, mask);
    }
    bool completed = measurement_pipeline_execute(&engine->pipeline, allow_break, mask);
    sweep_service_end_measurement();
    if (completed) {
      engine->last_cycle.completed = true;
      sweep_service_increment_generation();
      publish_event(engine->config.event_bus, EVENT_SWEEP_COMPLETED, &mask);
      if ((props_mode & DOMAIN_MODE) == DOMAIN_TIME) {
        app_measurement_transform_domain(mask);
      }
      request_to_redraw(REDRAW_PLOT);
    }
    if (engine->config.port.on_cycle_completed != NULL) {
      engine->config.port.on_cycle_completed(engine->config.port.context, &engine->last_cycle);
    }
  } else {
    sweep_service_end_measurement();
    if (ui_lever_repeat_pending()) {
      chThdSleepMilliseconds(5);
    } else {
      __WFI();
    }
  }
  return &engine->last_cycle;
}
