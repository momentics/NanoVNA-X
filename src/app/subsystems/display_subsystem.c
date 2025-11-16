#include "app/subsystems.h"

#include "platform/boards/stm32_peripherals.h"

#include <limits.h>

#include "ch.h"

#ifndef VBAT_MEASURE_INTERVAL
#define BATTERY_REDRAW_INTERVAL S2ST(5)
#else
#define BATTERY_REDRAW_INTERVAL VBAT_MEASURE_INTERVAL
#endif

static systime_t battery_next_sample = 0;
static int16_t battery_last_mv = INT16_MIN;

static void schedule_battery_redraw(void) {
  systime_t now = chVTGetSystemTimeX();
  if ((int32_t)(now - battery_next_sample) < 0) {
    return;
  }
  battery_next_sample = now + BATTERY_REDRAW_INTERVAL;
  int16_t vbat = adc_vbat_read();
  if (vbat == battery_last_mv) {
    return;
  }
  battery_last_mv = vbat;
  request_to_redraw(REDRAW_BATTERY);
}

void display_subsystem_init(void) {
  plot_init();
}

void display_subsystem_render(const sweep_subsystem_status_t* status) {
  (void)status;
  schedule_battery_redraw();
#ifndef DEBUG_CONSOLE_SHOW
  draw_all();
#endif
}
