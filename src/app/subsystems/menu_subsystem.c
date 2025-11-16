#include "app/subsystems.h"

#include "ui/ui_internal.h"

void menu_subsystem_init(void) {
#ifdef __FLIP_DISPLAY__
  if (VNA_MODE(VNA_MODE_FLIP_DISPLAY)) {
    lcd_set_flip(true);
  }
#endif
  ui_init();
}

void menu_subsystem_process(void) {
  sweep_mode |= SWEEP_UI_MODE;
  ui_process();
  sweep_mode &= (uint8_t)~SWEEP_UI_MODE;
}
