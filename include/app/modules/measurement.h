/*
 * Measurement subsystem port
 *
 * Wraps the existing sweep service helpers without changing their
 * behaviour while providing a minimal interface for the application.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "measurement/pipeline.h"

typedef struct {
  uint16_t (*active_mask)(measurement_pipeline_t* pipeline);
  void (*wait_for_copy_release)(void);
  void (*begin_measurement)(void);
  bool (*execute)(measurement_pipeline_t* pipeline, bool break_on_operation, uint16_t channel_mask);
  void (*end_measurement)(void);
  uint32_t (*increment_generation)(void);
} measurement_port_api_t;

typedef struct {
  measurement_pipeline_t* pipeline;
  const measurement_port_api_t* api;
} measurement_port_t;

typedef struct {
  measurement_pipeline_t* pipeline;
  measurement_port_t port;
} measurement_module_t;

void measurement_module_init(measurement_module_t* module, measurement_pipeline_t* pipeline);
const measurement_port_t* measurement_module_port(measurement_module_t* module);
