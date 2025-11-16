#include "app/modules/usb_command_server.h"

#include "app/shell.h"

#include "ch.h"

/*
 * CDC shell traffic routinely formats long responses (info, sweep dumps, etc.)
 * through chprintf().  Those formatters pull in a deep stack frame on top of
 * the line buffer below, so keep the worker stack comfortably above the 400+
 * byte footprint of the legacy implementation.
 */
#define USB_SHELL_THREAD_STACK_SIZE 512
static THD_WORKING_AREA(usb_shell_thread_wa, USB_SHELL_THREAD_STACK_SIZE);

static void default_write_prompt(void* context, const char* prompt) {
  (void)context;
  shell_printf("%s", prompt);
}

static void default_write_banner(void* context, const char* banner) {
  (void)context;
  shell_printf("%s", banner);
}

static bool default_check_connect(void* context) {
  (void)context;
  return shell_check_connect();
}

static int default_read_line(void* context, char* buffer, int length) {
  (void)context;
  return vna_shell_read_line(buffer, length);
}

static void default_on_session_hook(void* context) {
  (void)context;
}

static THD_FUNCTION(UsbShellThread, arg) {
  usb_command_server_t* server = (usb_command_server_t*)arg;
  usb_command_server_config_t* cfg = &server->config;
  chRegSetThreadName("usb");
  char line_buffer[VNA_SHELL_MAX_LENGTH];
  while (true) {
    if (cfg->check_connect(cfg->context)) {
      cfg->write_banner(cfg->context, VNA_SHELL_NEWLINE_STR "NanoVNA Shell" VNA_SHELL_NEWLINE_STR);
      cfg->on_session_start(cfg->context);
      do {
        cfg->write_prompt(cfg->context, VNA_SHELL_PROMPT_STR);
        if (cfg->read_line(cfg->context, line_buffer, VNA_SHELL_MAX_LENGTH)) {
          if (cfg->handler != NULL) {
            cfg->handler(line_buffer);
          }
        } else {
          chThdSleepMilliseconds(200);
        }
      } while (cfg->check_connect(cfg->context));
      cfg->on_session_end(cfg->context);
    }
    chThdSleepMilliseconds(1000);
  }
}

void usb_command_server_init(usb_command_server_t* server,
                             const usb_command_server_config_t* config) {
  chDbgAssert(server != NULL, "usb_command_server_init");
  chDbgAssert(config != NULL, "usb_command_server_init: config");
  server->config = *config;
  if (server->config.check_connect == NULL) {
    server->config.check_connect = default_check_connect;
  }
  if (server->config.read_line == NULL) {
    server->config.read_line = default_read_line;
  }
  if (server->config.write_prompt == NULL) {
    server->config.write_prompt = default_write_prompt;
  }
  if (server->config.write_banner == NULL) {
    server->config.write_banner = default_write_banner;
  }
  if (server->config.on_session_start == NULL) {
    server->config.on_session_start = default_on_session_hook;
  }
  if (server->config.on_session_end == NULL) {
    server->config.on_session_end = default_on_session_hook;
  }
  shell_register_commands(server->config.command_table);
  shell_init_connection();
  if (server->config.event_bus != NULL) {
    shell_attach_event_bus(server->config.event_bus);
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
  if (server == NULL || server->config.handler == NULL || line == NULL) {
    return;
  }
  server->config.handler(line);
}
