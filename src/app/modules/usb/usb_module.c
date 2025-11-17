#include "app/modules/usb.h"

void usb_server_module_init(usb_server_module_t* module) {
  if (module == NULL) {
    return;
  }
  static const usb_server_port_api_t usb_port_api = {
      .register_commands = shell_register_commands,
      .init_connection = shell_init_connection,
      .process_pending_commands = shell_service_pending_commands,
      .has_pending_io = shell_has_pending_io,
      .check_connection = shell_check_connect,
      .read_line = vna_shell_read_line,
      .execute_cmd_line = vna_shell_execute_cmd_line,
      .printf_fn = shell_printf,
  };
  module->port.context = module;
  module->port.api = &usb_port_api;
}

const usb_server_port_t* usb_server_module_port(usb_server_module_t* module) {
  if (module == NULL) {
    return NULL;
  }
  return &module->port;
}
