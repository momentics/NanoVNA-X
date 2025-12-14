/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */



/*
 * USB configuration stubs for host-side tests.  The real firmware pulls in the
 * ChibiOS USB stack; here we only need lightweight records so shell_service.c
 * can compile and so the tests can inspect the simulated connection state.
 */

#pragma once

#include "ch.h"

typedef struct {
  int state;
} USBDriver;

#define USB_ACTIVE 1

typedef struct {
  USBDriver* usbp;
} SerialUSBConfig;

typedef struct {
  BaseSequentialStream stream;
  SerialUSBConfig* config;
  void* user_data;
} SerialUSBDriver;

typedef struct {
  int dummy;
} USBConfig;

extern USBConfig usbcfg;
extern SerialUSBConfig serusbcfg;
extern SerialUSBDriver SDU1;
extern USBDriver USBD1;

void sduObjectInit(SerialUSBDriver* driver);
void sduStart(SerialUSBDriver* driver, const SerialUSBConfig* cfg);
void sduSuspendHookI(SerialUSBDriver* driver);
void sduWakeupHookI(SerialUSBDriver* driver);
void sduConfigureHookI(SerialUSBDriver* driver);
void usbDisconnectBus(USBDriver* driver);
void usbStart(USBDriver* driver, const USBConfig* cfg);
void usbConnectBus(USBDriver* driver);
