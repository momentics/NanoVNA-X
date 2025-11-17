/*
 * UI subsystem port
 *
 * Provides access to the existing UI/menu/input logic without changing
 * its reliance on global state.
 */

#pragma once

typedef struct {
  void (*ui_init)(void);
  void (*plot_init)(void);
  void (*process)(void);
  void (*schedule_battery_redraw)(void);
  void (*draw)(void);
} ui_port_api_t;

typedef struct {
  void* context;
  const ui_port_api_t* api;
} ui_port_t;

typedef struct {
  ui_port_t port;
} ui_module_t;

void ui_module_init(ui_module_t* module);
const ui_port_t* ui_module_port(ui_module_t* module);
