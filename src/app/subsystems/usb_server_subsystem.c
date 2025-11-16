#include "app/subsystems.h"

#include "app/shell.h"
#include "services/event_bus.h"

#include "ch.h"

static const VNAShellCommand* usb_shell_commands = NULL;

static THD_WORKING_AREA(waShellThread, 96);

static THD_FUNCTION(ShellServiceThread, arg) {
  (void)arg;
  chRegSetThreadName("usb");
  char shell_line[VNA_SHELL_MAX_LENGTH];
  while (true) {
    if (shell_check_connect()) {
      shell_printf(VNA_SHELL_NEWLINE_STR "NanoVNA Shell" VNA_SHELL_NEWLINE_STR);
      do {
        shell_printf(VNA_SHELL_PROMPT_STR);
        if (vna_shell_read_line(shell_line, VNA_SHELL_MAX_LENGTH)) {
          usb_server_handle_line(shell_line);
        } else {
          chThdSleepMilliseconds(200);
        }
      } while (shell_check_connect());
    }
    chThdSleepMilliseconds(1000);
  }
}

void usb_server_subsystem_init(const VNAShellCommand* command_table, event_bus_t* bus) {
  usb_shell_commands = command_table;
  shell_register_commands(usb_shell_commands);
  shell_init_connection();
  if (bus != NULL) {
    shell_attach_event_bus(bus);
  }
}

void usb_server_subsystem_start(void) {
  chThdCreateStatic(waShellThread, sizeof(waShellThread), NORMALPRIO - 1, ShellServiceThread, NULL);
}

void usb_server_subsystem_service(void) {
  shell_service_pending_commands();
}
