#include "app/subsystems.h"

#include "app/sweep_service.h"
#include "measurement/pipeline.h"
#include "services/event_bus.h"
#include "ui/ui_internal.h"

#include "ch.h"

typedef struct {
  measurement_pipeline_t pipeline;
  event_bus_t* bus;
  sweep_subsystem_status_t status;
} sweep_subsystem_context_t;

static sweep_subsystem_context_t sweep_ctx;

void sweep_subsystem_init(const PlatformDrivers* drivers, event_bus_t* bus) {
  sweep_ctx.bus = bus;
  measurement_pipeline_init(&sweep_ctx.pipeline, drivers);
  sweep_service_init();
  sweep_ctx.status.completed = false;
  sweep_ctx.status.mask = 0;
}

static void publish_event(event_bus_topic_t topic, uint16_t* mask) {
  if (sweep_ctx.bus != NULL) {
    event_bus_publish(sweep_ctx.bus, topic, mask);
  }
}

const sweep_subsystem_status_t* sweep_subsystem_cycle(void) {
  sweep_ctx.status.completed = false;
  uint16_t mask = measurement_pipeline_active_mask(&sweep_ctx.pipeline);
  sweep_ctx.status.mask = mask;
  if ((sweep_mode & (SWEEP_ENABLE | SWEEP_ONCE)) && !sweep_control_is_holding()) {
    sweep_service_wait_for_copy_release();
    sweep_service_begin_measurement();
    publish_event(EVENT_SWEEP_STARTED, &mask);
    bool completed = measurement_pipeline_execute(&sweep_ctx.pipeline, true, mask);
    sweep_mode &= (uint8_t)~SWEEP_ONCE;
    sweep_service_end_measurement();
    if (completed) {
      sweep_ctx.status.completed = true;
      sweep_service_increment_generation();
      publish_event(EVENT_SWEEP_COMPLETED, &mask);
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
  return &sweep_ctx.status;
}
