#include "modules/usb/usb_server_port.h"

const usb_server_port_api_t usb_port_api = {
    .register_commands = shell_register_commands,
    .printf = shell_printf,
    .stream_write = shell_stream_write,
    .update_speed = shell_update_speed,
    .check_connect = shell_check_connect,
    .init_connection = shell_init_connection,
    .parse_command = shell_parse_command,
    .request_deferred_execution = shell_request_deferred_execution,
    .service_pending_commands = shell_service_pending_commands,
    .read_line = vna_shell_read_line,
    .execute_cmd_line = vna_shell_execute_cmd_line};
