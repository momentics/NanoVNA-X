#include "modules/ui/ui_port.h"

const ui_module_port_api_t ui_port_api = {.init = ui_init,
                                          .process = ui_process,
                                          .enter_dfu = ui_enter_dfu,
                                          .touch_cal_exec = ui_touch_cal_exec,
                                          .touch_draw_test = ui_touch_draw_test,
                                          .message_box = ui_message_box};
