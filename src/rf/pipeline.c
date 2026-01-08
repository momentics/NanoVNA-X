/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Originally written using elements from Dmitry (DiSlord) dislordlive@gmail.com
 * Originally written using elements from TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
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

#include "rf/pipeline.h"
#include <stddef.h>

#include "rf/sweep.h"

void measurement_pipeline_init(measurement_pipeline_t* pipeline, const PlatformDrivers* drivers) {
  if (pipeline == NULL) {
    return;
  }
  pipeline->drivers = drivers;
}

uint16_t measurement_pipeline_active_mask(measurement_pipeline_t* pipeline) {
  (void)pipeline;
  return app_measurement_get_sweep_mask();
}

bool measurement_pipeline_execute(measurement_pipeline_t* pipeline, bool break_on_operation,
                                  uint16_t channel_mask) {
  (void)pipeline;
  return app_measurement_sweep(break_on_operation, channel_mask);
}
