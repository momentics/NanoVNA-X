/*
 * Measurement subsystem port definition.
 *
 * Formalizes access to the measurement pipeline and sweep service without
 * touching the legacy implementation.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/sweep_service.h"
#include "measurement/pipeline.h"
#include "platform/hal.h"

typedef struct measurement_module_context {
  measurement_pipeline_t pipeline;
} measurement_module_context_t;

struct measurement_port;
typedef struct measurement_port measurement_port_t;

typedef void (*measurement_sample_func_t)(float* gamma);

typedef struct measurement_port_api {
  void (*pipeline_init)(measurement_module_context_t* context, const PlatformDrivers* drivers);
  uint16_t (*active_mask)(measurement_module_context_t* context);
  bool (*execute)(measurement_module_context_t* context, bool break_on_operation,
                  uint16_t channel_mask);
  void (*service_init)(void);
  void (*wait_for_copy_release)(void);
  void (*begin_measurement)(void);
  void (*end_measurement)(void);
  uint32_t (*increment_generation)(void);
  void (*wait_for_generation)(void);
  void (*reset_progress)(void);
  bool (*snapshot_acquire)(uint8_t channel, sweep_service_snapshot_t* snapshot);
  bool (*snapshot_release)(const sweep_service_snapshot_t* snapshot);
  void (*start_capture)(systime_t delay_ticks);
  bool (*wait_for_capture)(void);
  const audio_sample_t* (*rx_buffer)(void);
#if ENABLED_DUMP_COMMAND
  void (*prepare_dump)(audio_sample_t* buffer, size_t count, int selection);
  bool (*dump_ready)(void);
#endif
  uint16_t (*sweep_mask)(void);
  bool (*sweep)(bool break_on_operation, uint16_t mask);
  int (*set_frequency)(freq_t freq);
  void (*set_frequencies)(freq_t start, freq_t stop, uint16_t points);
  void (*update_frequencies)(void);
  void (*transform_domain)(uint16_t ch_mask);
  void (*set_sample_function)(measurement_sample_func_t func);
  void (*set_smooth_factor)(uint8_t factor);
  uint8_t (*get_smooth_factor)(void);
} measurement_port_api_t;

struct measurement_port {
  measurement_module_context_t* context;
  const measurement_port_api_t* api;
};

extern const measurement_port_api_t measurement_port_api;
