/*
 * High-level subsystem interfaces for NanoVNA-X.
 *
 * Each subsystem encapsulates a functional domain of the firmware so the
 * application scheduler can orchestrate them explicitly.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nanovna.h"
#include "services/event_bus.h"
#include "app/shell.h"
#include "measurement/pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool completed;
  uint16_t mask;
} sweep_subsystem_status_t;

void sweep_subsystem_init(const PlatformDrivers* drivers, event_bus_t* bus);
const sweep_subsystem_status_t* sweep_subsystem_cycle(void);

void display_subsystem_init(void);
void display_subsystem_render(const sweep_subsystem_status_t* status);

void menu_subsystem_init(void);
void menu_subsystem_process(void);

void usb_server_subsystem_init(const VNAShellCommand* command_table, event_bus_t* bus);
void usb_server_subsystem_start(void);
void usb_server_subsystem_service(void);
void usb_server_handle_line(char* line);

#ifdef __cplusplus
}
#endif
