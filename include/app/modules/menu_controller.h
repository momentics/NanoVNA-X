/*
 * Menu controller: handles UI state machines and user inputs.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void* context;
  void (*ui_init)(void* context);
  void (*ui_process)(void* context);
  bool (*should_flip_display)(void* context);
  void (*set_display_flip)(void* context, bool enable);
} menu_controller_port_t;

typedef struct menu_controller {
  menu_controller_port_t port;
} menu_controller_t;

void menu_controller_init(menu_controller_t* controller, const menu_controller_port_t* port);
void menu_controller_process(menu_controller_t* controller);

#ifdef __cplusplus
}
#endif
