/*
 * Menu controller: handles UI state machines and user inputs.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct menu_controller {
  uint8_t reserved;
} menu_controller_t;

void menu_controller_init(menu_controller_t* controller);
void menu_controller_process(menu_controller_t* controller);

#ifdef __cplusplus
}
#endif
