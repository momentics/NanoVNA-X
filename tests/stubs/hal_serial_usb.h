#pragma once
#include "ch.h"

// Stub for hal_serial_usb.h

typedef struct USBDriver USBDriver;
typedef struct SerialUSBConfig SerialUSBConfig;
typedef struct USBConfig USBConfig;

typedef struct SerialUSBDriver {
  BaseAsynchronousChannel stream;
  const SerialUSBConfig *config;
  void *user_data;
} SerialUSBDriver;

struct USBDriver {
  int state;
};

struct SerialUSBConfig {
  USBDriver *usbp;
};

struct USBConfig {
  int dummy;
};

extern SerialUSBDriver SDU1;
extern USBDriver USBD1;
extern SerialUSBConfig SERUSBCFG;
extern USBConfig USBCFG;

#define USB_ACTIVE 1

void sduObjectInit(SerialUSBDriver *p);
void sduStart(SerialUSBDriver *p, const SerialUSBConfig *c);
void usbDisconnectBus(USBDriver *p);
void usbStart(USBDriver *p, const USBConfig *c);
void usbConnectBus(USBDriver *p);
