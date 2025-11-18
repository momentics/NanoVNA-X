/*
 * Processing subsystem port definition.
 *
 * Provides access to DSP helpers without modifying legacy implementation.
 */

#pragma once

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

