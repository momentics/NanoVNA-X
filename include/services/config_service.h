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

#pragma once

#include <stdint.h>

#include "services/event_bus.h"

typedef struct {
  int (*save_configuration)(void);
  int (*load_configuration)(void);
  int (*save_calibration)(uint32_t slot);
  int (*load_calibration)(uint32_t slot);
  void (*erase_calibration)(void);
} config_service_api_t;

const config_service_api_t* config_service_api(void);
void config_service_init(void);
/**
 * @brief Binds the configuration service to an event bus.
 *
 * Once attached, the service emits EVENT_STORAGE_UPDATED notifications after
 * persisting data so UI components can refresh calibration indicators.
 */
void config_service_attach_event_bus(event_bus_t* bus);
void config_service_notify_configuration_changed(void);



