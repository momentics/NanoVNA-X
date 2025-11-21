/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "interfaces/ports/ui_port.h"

const ui_module_port_api_t ui_port_api = {.init = ui_init,
                                          .process = ui_process,
                                          .enter_dfu = ui_enter_dfu,
                                          .touch_cal_exec = ui_touch_cal_exec,
                                          .touch_draw_test = ui_touch_draw_test,
                                          .message_box = ui_message_box};
