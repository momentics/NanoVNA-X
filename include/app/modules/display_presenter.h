/*
 * Display presenter: translates measurement state into LCD updates.
 */

#pragma once

#include <stdint.h>

#include "app/modules/measurement_engine.h"
#include "ch.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void* context;
  uint16_t (*read_battery_mv)(void* context);
  void (*request_redraw)(void* context, uint16_t area);
  void (*draw_all)(void* context);
  void (*plot_init)(void* context);
} display_presenter_port_t;

typedef struct display_presenter {
  systime_t next_refresh;
  int16_t last_battery_mv;
  display_presenter_port_t port;
} display_presenter_t;

void display_presenter_init(display_presenter_t* presenter, const display_presenter_port_t* port);
void display_presenter_render(display_presenter_t* presenter,
                              const measurement_cycle_result_t* last_cycle);

#ifdef __cplusplus
}
#endif
