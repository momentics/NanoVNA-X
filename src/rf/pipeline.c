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

/* @jebcampbellhm, PR #13: use the calibration reference, not a fixed 50 ohms. */
void measurement_pipeline_renormalize(uint16_t channel_mask) {
#ifdef __VNA_Z_RENORMALIZATION__
  if ((channel_mask & SWEEP_CH0_MEASURE) && current_props._portz > 1.0f &&
      cal_load_r > 1.0f && current_props._portz != cal_load_r) {
    float k = (current_props._portz - cal_load_r) / (current_props._portz + cal_load_r);
    for (unsigned i = 0; i < sweep_points; ++i) {
      float re = measured[0][i][0], im = measured[0][i][1];
      float a = re - k, c = 1.0f - re * k, d = -im * k;
      float den = c * c + d * d;
      if (den > 1e-9f) {
        measured[0][i][0] = (a * c + im * d) / den;
        measured[0][i][1] = (im * c - a * d) / den;
      }
    }
  }
#else
  (void)channel_mask;
#endif
}

bool measurement_pipeline_execute(measurement_pipeline_t* pipeline, bool break_on_operation,
                                  uint16_t channel_mask) {
  (void)pipeline;
  bool res = app_measurement_sweep(break_on_operation, channel_mask);

  if (res) measurement_pipeline_renormalize(channel_mask);

#ifdef __USE_SMOOTH__
  if (res)
    measurement_data_smooth(channel_mask);
#endif

  return res;
}
