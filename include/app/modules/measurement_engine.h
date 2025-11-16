/*
 * Measurement engine: orchestrates sweep execution and DSP post-processing.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/sweep_service.h"
#include "measurement/pipeline.h"
#include "services/event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Captures the outcome of the most recent measurement cycle.
 */
typedef struct {
  bool completed;
  uint16_t active_channels;
} measurement_cycle_result_t;

/**
 * @brief Ports used by the measurement engine to interact with the host layer.
 */
typedef struct {
  void* context;
  bool (*is_sweep_enabled)(void* context);
  void (*on_cycle_started)(void* context, uint16_t channel_mask);
  void (*on_cycle_completed)(void* context, const measurement_cycle_result_t* result);
} measurement_engine_port_t;

/**
 * @brief Configuration used to bootstrap the measurement engine.
 */
typedef struct {
  const PlatformDrivers* drivers;
  event_bus_t* event_bus;
  measurement_engine_port_t port;
} measurement_engine_config_t;

typedef struct {
  measurement_pipeline_t pipeline;
  measurement_cycle_result_t last_cycle;
  measurement_engine_config_t config;
} measurement_engine_t;

void measurement_engine_init(measurement_engine_t* engine, const measurement_engine_config_t* config);
const measurement_cycle_result_t* measurement_engine_execute(measurement_engine_t* engine, bool allow_break);

#ifdef __cplusplus
}
#endif
