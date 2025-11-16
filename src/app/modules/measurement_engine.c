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
                             const PlatformDrivers* drivers,
                             event_bus_t* bus) {
  chDbgAssert(engine != NULL, "measurement_engine_init: engine");
  engine->bus = bus;
  engine->last_cycle.completed = false;
  engine->last_cycle.active_channels = 0;
  measurement_pipeline_init(&engine->pipeline, drivers);
  sweep_service_init();
}

const measurement_cycle_result_t* measurement_engine_execute(measurement_engine_t* engine) {
  chDbgAssert(engine != NULL, "measurement_engine_execute: engine");
  engine->last_cycle.completed = false;
  uint16_t mask = measurement_pipeline_active_mask(&engine->pipeline);
  engine->last_cycle.active_channels = mask;
  if ((sweep_mode & (SWEEP_ENABLE | SWEEP_ONCE)) && !sweep_control_is_holding()) {
    sweep_service_wait_for_copy_release();
    sweep_service_begin_measurement();
    publish_event(engine->bus, EVENT_SWEEP_STARTED, &mask);
    bool completed = measurement_pipeline_execute(&engine->pipeline, true, mask);
    sweep_mode &= (uint8_t)~SWEEP_ONCE;
    sweep_service_end_measurement();
    if (completed) {
      engine->last_cycle.completed = true;
      sweep_service_increment_generation();
      publish_event(engine->bus, EVENT_SWEEP_COMPLETED, &mask);
      if ((props_mode & DOMAIN_MODE) == DOMAIN_TIME) {
        app_measurement_transform_domain(mask);
      }
      request_to_redraw(REDRAW_PLOT);
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
