#include "app/modules/display_presenter.h"

#include "nanovna.h"
#include "platform/boards/stm32_peripherals.h"

#include "ch.h"

#include <limits.h>

#ifndef VBAT_MEASURE_INTERVAL
#define BATTERY_REDRAW_INTERVAL S2ST(5)
#else
#define BATTERY_REDRAW_INTERVAL VBAT_MEASURE_INTERVAL
#endif

static void refresh_battery_indicator(display_presenter_t* presenter) {
  systime_t now = chVTGetSystemTimeX();
  if ((int32_t)(now - presenter->next_refresh) < 0) {
    return;
  }
  presenter->next_refresh = now + BATTERY_REDRAW_INTERVAL;
  int16_t vbat = adc_vbat_read();
  if (vbat == presenter->last_battery_mv) {
    return;
  }
  presenter->last_battery_mv = vbat;
  request_to_redraw(REDRAW_BATTERY);
}

void display_presenter_init(display_presenter_t* presenter) {
  chDbgAssert(presenter != NULL, "display_presenter_init");
  presenter->next_refresh = 0;
  presenter->last_battery_mv = INT16_MIN;
  plot_init();
}

void display_presenter_render(display_presenter_t* presenter,
                              const measurement_cycle_result_t* last_cycle) {
  (void)last_cycle;
  chDbgAssert(presenter != NULL, "display_presenter_render");
  refresh_battery_indicator(presenter);
#ifndef DEBUG_CONSOLE_SHOW
  draw_all();
#endif
}
