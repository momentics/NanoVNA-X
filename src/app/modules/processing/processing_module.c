#include "app/modules/processing.h"

#include "app/sweep_service.h"
#include "nanovna.h"
#include "system/state_manager.h"

void processing_module_init(processing_module_t* module, event_bus_t* bus) {
  if (module == NULL) {
    return;
  }
  static const processing_port_api_t processing_port_api = {
      .publish = event_bus_publish,
      .transform_domain = app_measurement_transform_domain,
      .request_redraw = request_to_redraw,
      .state_service = state_manager_service,
  };
  module->bus = bus;
  module->port.bus = bus;
  module->port.api = &processing_port_api;
}

const processing_port_t* processing_module_port(processing_module_t* module) {
  if (module == NULL) {
    return NULL;
  }
  return &module->port;
}
