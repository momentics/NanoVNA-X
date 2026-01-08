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
#include "nanovna.h"
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
  bool res = app_measurement_sweep(break_on_operation, channel_mask);

#ifdef __VNA_Z_RENORMALIZATION__
  if (res && current_props._portz != 50.0f && current_props._portz > 1.0f) {
     float k = (current_props._portz - 50.0f) / (current_props._portz + 50.0f);
     uint16_t count = sweep_points;
     // Only renormalize CH0 (S11)
     if (channel_mask & 1) {
       for (uint16_t i = 0; i < count; i++) {
         float re = measured[0][i][0];
         float im = measured[0][i][1];
         
         // Gamma_new = (Gamma - k) / (1 - Gamma*k)
         // A + jB = (re - k) + j(im)
         // C + jD = (1 - re*k) + j(-im*k)
         
         float A = re - k;
         float B = im;
         float C = 1.0f - re * k;
         float D = -im * k;
         
         float den = C * C + D * D;
         if (den > 1e-9f) {
           measured[0][i][0] = (A * C + B * D) / den;
           measured[0][i][1] = (B * C - A * D) / den;
         }
       }
     }
  }
#endif

  return res;}
