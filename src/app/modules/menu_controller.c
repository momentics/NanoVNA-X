#include "app/modules/menu_controller.h"

#include "nanovna.h"
#include "ui/ui_internal.h"

void menu_controller_init(menu_controller_t* controller) {
  (void)controller;
#ifdef __FLIP_DISPLAY__
  if (VNA_MODE(VNA_MODE_FLIP_DISPLAY)) {
    lcd_set_flip(true);
  }
#endif
  ui_init();
}

void menu_controller_process(menu_controller_t* controller) {
  (void)controller;
  sweep_mode |= SWEEP_UI_MODE;
  ui_process();
  sweep_mode &= (uint8_t)~SWEEP_UI_MODE;
}
