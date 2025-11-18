/*
 * UI/Menu/Input subsystem port definition.
 */

#pragma once

#include <stdint.h>

#include "nanovna.h"


typedef struct ui_module_port ui_module_port_t;

typedef struct ui_module_port_api {
  void (*init)(void);
  void (*process)(void);
  void (*enter_dfu)(void);
  void (*touch_cal_exec)(void);
  void (*touch_draw_test)(void);
  void (*message_box)(const char* header, const char* text, uint32_t delay_ms);
} ui_module_port_api_t;

struct ui_module_port {
  void* context;
  const ui_module_port_api_t* api;
};

extern const ui_module_port_api_t ui_port_api;

