#pragma once
#include "hal_serial_usb.h"

// Externs for compatibility if needed (but prefer Uppercase)
extern USBConfig usbcfg;
extern SerialUSBConfig serusbcfg;
extern SerialUSBDriver sd_u1;
extern USBDriver usb_d1;

// Function prototypes if needed (but handled by hal_serial_usb.h usually)
// If shell_service.c calls lowercase functions?
// I renamed usages in shell_service.c?
// No, I only renamed globals.
// ChibiOS functions are sduStart etc.
// usbcfg.h had definitions of sdu_object_init...
// If usage is lowercase, I need them.
// But test implementation uses sduStart (CamelCase).
// I believe codebase uses CamelCase for functions.
