#include "app/modules/menu_controller.h"

#include "nanovna.h"
#include "ui/ui_internal.h"

static bool default_should_flip(void* context) {
  (void)context;
#ifdef __FLIP_DISPLAY__
  return VNA_MODE(VNA_MODE_FLIP_DISPLAY) != 0;
#else
  return false;
#endif
}

static void default_set_flip(void* context, bool enable) {
  (void)context;
#ifdef __FLIP_DISPLAY__
  lcd_set_flip(enable);
#else
  (void)enable;
#endif
}

void menu_controller_init(menu_controller_t* controller, const menu_controller_port_t* port) {
  chDbgAssert(controller != NULL, "menu_controller_init");
  chDbgAssert(port != NULL, "menu_controller_init: port");
  controller->port = *port;
  if (controller->port.should_flip_display == NULL) {
    controller->port.should_flip_display = default_should_flip;
  }
  if (controller->port.set_display_flip == NULL) {
    controller->port.set_display_flip = default_set_flip;
  }
  if (controller->port.should_flip_display(controller->port.context)) {
    controller->port.set_display_flip(controller->port.context, true);
  }
  if (controller->port.ui_init != NULL) {
    controller->port.ui_init(controller->port.context);
  }
}

void menu_controller_process(menu_controller_t* controller) {
  chDbgAssert(controller != NULL, "menu_controller_process");
  sweep_mode |= SWEEP_UI_MODE;
  if (controller->port.ui_process != NULL) {
    controller->port.ui_process(controller->port.context);
  }
  sweep_mode &= (uint8_t)~SWEEP_UI_MODE;
}
