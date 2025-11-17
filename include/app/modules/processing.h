/*
 * Processing subsystem port
 *
 * Encapsulates event bus notifications and post-measurement processing.
 */

#pragma once

#include <stdint.h>

#include "services/event_bus.h"

typedef struct {
  bool (*publish)(event_bus_t* bus, event_bus_topic_t topic, const void* payload);
  void (*transform_domain)(uint16_t mask);
  void (*request_redraw)(uint16_t flags);
  void (*state_service)(void);
} processing_port_api_t;

typedef struct {
  event_bus_t* bus;
  const processing_port_api_t* api;
} processing_port_t;

typedef struct {
  event_bus_t* bus;
  processing_port_t port;
} processing_module_t;

void processing_module_init(processing_module_t* module, event_bus_t* bus);
const processing_port_t* processing_module_port(processing_module_t* module);
