#include "app/modules/usb_command_server.h"

#include "app/shell.h"

#include "ch.h"

static THD_WORKING_AREA(usb_shell_thread_wa, 96);

static THD_FUNCTION(UsbShellThread, arg) {
  usb_command_server_t* server = (usb_command_server_t*)arg;
  chRegSetThreadName("usb");
  char line_buffer[VNA_SHELL_MAX_LENGTH];
  while (true) {
    if (shell_check_connect()) {
      shell_printf(VNA_SHELL_NEWLINE_STR "NanoVNA Shell" VNA_SHELL_NEWLINE_STR);
      do {
        shell_printf(VNA_SHELL_PROMPT_STR);
        if (vna_shell_read_line(line_buffer, VNA_SHELL_MAX_LENGTH)) {
          if (server->handler != NULL) {
            server->handler(line_buffer);
          }
        } else {
          chThdSleepMilliseconds(200);
        }
      } while (shell_check_connect());
    }
    chThdSleepMilliseconds(1000);
  }
}

void usb_command_server_init(usb_command_server_t* server,
                             const VNAShellCommand* command_table,
                             event_bus_t* bus,
                             usb_command_handler_t handler) {
  chDbgAssert(server != NULL, "usb_command_server_init");
  server->commands = command_table;
  server->bus = bus;
  server->handler = handler;
  shell_register_commands(server->commands);
  shell_init_connection();
  if (bus != NULL) {
    shell_attach_event_bus(bus);
  }
}

void usb_command_server_start(usb_command_server_t* server) {
  chDbgAssert(server != NULL, "usb_command_server_start");
  (void)chThdCreateStatic(usb_shell_thread_wa, sizeof(usb_shell_thread_wa),
                          NORMALPRIO - 1, UsbShellThread, server);
}

void usb_command_server_service(usb_command_server_t* server) {
  (void)server;
  shell_service_pending_commands();
}

void usb_command_server_dispatch_line(usb_command_server_t* server, char* line) {
  if (server == NULL || server->handler == NULL || line == NULL) {
    return;
  }
  server->handler(line);
}
