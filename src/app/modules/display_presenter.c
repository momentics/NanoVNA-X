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

static uint16_t default_read_battery(void* context) {
  (void)context;
  return adc_vbat_read();
}

static void default_request_redraw(void* context, uint16_t area) {
  (void)context;
  request_to_redraw(area);
}

static void default_draw_all(void* context) {
  (void)context;
#ifndef DEBUG_CONSOLE_SHOW
  draw_all();
#endif
}

static void default_plot_init(void* context) {
  (void)context;
  plot_init();
}

static void refresh_battery_indicator(display_presenter_t* presenter) {
  systime_t now = chVTGetSystemTimeX();
  if ((int32_t)(now - presenter->next_refresh) < 0) {
    return;
  }
  presenter->next_refresh = now + BATTERY_REDRAW_INTERVAL;
  uint16_t vbat =
      presenter->port.read_battery_mv ? presenter->port.read_battery_mv(presenter->port.context) : 0;
  if (vbat == presenter->last_battery_mv) {
    return;
  }
  presenter->last_battery_mv = vbat;
  if (presenter->port.request_redraw != NULL) {
    presenter->port.request_redraw(presenter->port.context, REDRAW_BATTERY);
  }
}

void display_presenter_init(display_presenter_t* presenter, const display_presenter_port_t* port) {
  chDbgAssert(presenter != NULL, "display_presenter_init");
  chDbgAssert(port != NULL, "display_presenter_init: port");
  presenter->next_refresh = 0;
  presenter->last_battery_mv = INT16_MIN;
  presenter->port = *port;
  if (presenter->port.read_battery_mv == NULL) {
    presenter->port.read_battery_mv = default_read_battery;
  }
  if (presenter->port.request_redraw == NULL) {
    presenter->port.request_redraw = default_request_redraw;
  }
  if (presenter->port.draw_all == NULL) {
    presenter->port.draw_all = default_draw_all;
  }
  if (presenter->port.plot_init == NULL) {
    presenter->port.plot_init = default_plot_init;
  }
  presenter->port.plot_init(presenter->port.context);
}

void display_presenter_render(display_presenter_t* presenter,
                              const measurement_cycle_result_t* last_cycle) {
  (void)last_cycle;
  chDbgAssert(presenter != NULL, "display_presenter_render");
  refresh_battery_indicator(presenter);
  presenter->port.draw_all(presenter->port.context);
}
