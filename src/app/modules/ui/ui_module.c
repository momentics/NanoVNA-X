/*
 * UI subsystem module
 *
 * Collects the pieces of UI initialisation and periodic maintenance that
 * used to live directly in the application loop.
 */

#include "app/modules/ui.h"

#include "ch.h"
#include "nanovna.h"
#include "platform/boards/stm32_peripherals.h"

#include <limits.h>
#include <stdint.h>

static int16_t battery_last_mv = INT16_MIN;
static systime_t battery_next_sample = 0;

#ifndef VBAT_MEASURE_INTERVAL
#define BATTERY_REDRAW_INTERVAL S2ST(5)
#else
#define BATTERY_REDRAW_INTERVAL VBAT_MEASURE_INTERVAL
#endif

static void ui_port_schedule_battery(void) {
  const systime_t now = chVTGetSystemTimeX();
  if ((int32_t)(now - battery_next_sample) < 0) {
    return;
  }
  battery_next_sample = now + BATTERY_REDRAW_INTERVAL;
  const int16_t vbat = adc_vbat_read();
  if (vbat == battery_last_mv) {
    return;
  }
  battery_last_mv = vbat;
  request_to_redraw(REDRAW_BATTERY);
}

static void ui_port_draw_all(void) {
#ifndef DEBUG_CONSOLE_SHOW
  draw_all();
#endif
}

void ui_module_init(ui_module_t* module) {
  if (module == NULL) {
    return;
  }
  static const ui_port_api_t ui_port_api = {
      .ui_init = ui_init,
      .plot_init = plot_init,
      .process = ui_process,
      .schedule_battery_redraw = ui_port_schedule_battery,
      .draw = ui_port_draw_all,
  };
  module->port.context = module;
  module->port.api = &ui_port_api;
}

const ui_port_t* ui_module_port(ui_module_t* module) {
  if (module == NULL) {
    return NULL;
  }
  return &module->port;
}
