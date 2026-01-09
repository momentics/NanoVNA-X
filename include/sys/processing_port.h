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





#ifndef __SYS_PROCESSING_PORT_H__
#define __SYS_PROCESSING_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "nanovna.h"

typedef struct processing_port processing_port_t;
typedef void (*processing_sample_func_t)(float* gamma);

typedef struct processing_port_api {
  processing_sample_func_t calculate_gamma;
  processing_sample_func_t fetch_amplitude;
  processing_sample_func_t fetch_amplitude_ref;
} processing_port_api_t;

struct processing_port {
  void* context;
  const processing_port_api_t* api;
};

extern const processing_port_api_t processing_port_api;

#ifdef __cplusplus
}
#endif

#endif // __SYS_PROCESSING_PORT_H__
