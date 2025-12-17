/*
    ChibiOS - Copyright (C) 2006..2015 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "hal.h"
#include "nanovna.h"
#include "interfaces/cli/shell_service.h"
#include "hal_usb_cdc.h"

/* Virtual serial port over USB.*/
SerialUSBDriver SDU1;

/*
 * Static line coding variable, required by the CDC implementation.
 */
static cdc_linecoding_t linecoding = {{0x00, 0x40, 0x38, 0x00}, LC_STOP_1, LC_PARITY_NONE, 8};

enum { STR_LANG_ID = 0, STR_MANUFACTURER, STR_PRODUCT, STR_SERIAL };
/*
 * Endpoints to be used for USBD1.
 */
#define USBD1_DATA_REQUEST_EP 1
#define USBD1_DATA_AVAILABLE_EP 1
#define USBD1_INTERRUPT_REQUEST_EP 2

/*
 * USB Device Descriptor.
 */
static const uint8_t vcom_device_descriptor_data[18] = {
    USB_DESC_DEVICE(0x0110,           /* bcdUSB (1.1).                    */
                    0x02,             /* bDeviceClass (CDC).              */
                    0x00,             /* bDeviceSubClass.                 */
                    0x00,             /* bDeviceProtocol.                 */
                    0x40,             /* bMaxPacketSize.                  */
                    0x0483,           /* idVendor (ST).                   */
                    0x5740,           /* idProduct.                       */
                    0x0200,           /* bcdDevice.                       */
                    STR_MANUFACTURER, /* iManufacturer.                   */
                    STR_PRODUCT,      /* iProduct.                        */
                    STR_SERIAL,       /* iSerialNumber.                   */
                    1)                /* bNumConfigurations.              */
};

/*
 * Device Descriptor wrapper.
 */
static const USBDescriptor vcom_device_descriptor = {sizeof vcom_device_descriptor_data,
                                                     vcom_device_descriptor_data};

/* Configuration Descriptor tree for a CDC.*/
static const uint8_t vcom_configuration_descriptor_data[67] = {
    /* Configuration Descriptor.*/
    USB_DESC_CONFIGURATION(67,       /* wTotalLength.                    */
                           0x02,     /* bNumInterfaces.                  */
                           0x01,     /* bConfigurationValue.             */
                           0,        /* iConfiguration.                  */
                           0xC0,     /* bmAttributes (self powered).     */
                           500 / 2), /* bMaxPower in 2mA units (500mA).  */
    /* Interface Descriptor.*/
    USB_DESC_INTERFACE(0x00, /* bInterfaceNumber.                */
                       0x00, /* bAlternateSetting.               */
                       0x01, /* bNumEndpoints.                   */
                       0x02, /* bInterfaceClass (Communications
                                Interface Class, CDC section
                                4.2).                            */
                       0x02, /* bInterfaceSubClass (Abstract
                              Control Model, CDC section 4.3).   */
                       0x00, /* bInterfaceProtocol (No protocol,
                                CDC section 4.4).                */
                       0),   /* iInterface.                      */
    /* Header Functional Descriptor (CDC section 5.2.3).*/
    USB_DESC_BYTE(5),     /* bLength.                         */
    USB_DESC_BYTE(0x24),  /* bDescriptorType (CS_INTERFACE).  */
    USB_DESC_BYTE(0x00),  /* bDescriptorSubtype (Header
                             Functional Descriptor.           */
    USB_DESC_BCD(0x0110), /* bcdCDC.                          */
    /* Call Management Functional Descriptor. */
    USB_DESC_BYTE(5),    /* bFunctionLength.                 */
    USB_DESC_BYTE(0x24), /* bDescriptorType (CS_INTERFACE).  */
    USB_DESC_BYTE(0x01), /* bDescriptorSubtype (Call Management
                            Functional Descriptor).          */
    USB_DESC_BYTE(0x00), /* bmCapabilities (D0+D1).          */
    USB_DESC_BYTE(0x01), /* bDataInterface.                  */
    /* ACM Functional Descriptor.*/
    USB_DESC_BYTE(4),    /* bFunctionLength.                 */
    USB_DESC_BYTE(0x24), /* bDescriptorType (CS_INTERFACE).  */
    USB_DESC_BYTE(0x02), /* bDescriptorSubtype (Abstract
                            Control Management Descriptor).  */
    USB_DESC_BYTE(0x02), /* bmCapabilities.                  */
    /* Union Functional Descriptor.*/
    USB_DESC_BYTE(5),    /* bFunctionLength.                 */
    USB_DESC_BYTE(0x24), /* bDescriptorType (CS_INTERFACE).  */
    USB_DESC_BYTE(0x06), /* bDescriptorSubtype (Union
                            Functional Descriptor).          */
    USB_DESC_BYTE(0x00), /* bMasterInterface (Communication
                            Class Interface).                */
    USB_DESC_BYTE(0x01), /* bSlaveInterface0 (Data Class
                            Interface).                      */
    /* Endpoint 2 Descriptor.*/
    USB_DESC_ENDPOINT(USBD1_INTERRUPT_REQUEST_EP | 0x80, 0x03, /* bmAttributes (Interrupt). */
                      0x0008, /* wMaxPacketSize.                  */
                      0xFF),  /* bInterval.                       */
    /* Interface Descriptor.*/
    USB_DESC_INTERFACE(0x01,  /* bInterfaceNumber.                */
                       0x00,  /* bAlternateSetting.               */
                       0x02,  /* bNumEndpoints.                   */
                       0x0A,  /* bInterfaceClass (Data Class
                                 Interface, CDC section 4.5).     */
                       0x00,  /* bInterfaceSubClass (CDC section
                                 4.6).                            */
                       0x00,  /* bInterfaceProtocol (CDC section
                                 4.7).                            */
                       0x00), /* iInterface.                      */
    /* Endpoint 3 Descriptor.*/
    USB_DESC_ENDPOINT(USBD1_DATA_AVAILABLE_EP, /* bEndpointAddress.*/
                      0x02,                    /* bmAttributes (Bulk).             */
                      0x0040,                  /* wMaxPacketSize.                  */
                      0x00),                   /* bInterval.                       */
    /* Endpoint 1 Descriptor.*/
    USB_DESC_ENDPOINT(USBD1_DATA_REQUEST_EP | 0x80, /* bEndpointAddress.*/
                      0x02,                         /* bmAttributes (Bulk).             */
                      0x0040,                       /* wMaxPacketSize.                  */
                      0x00)                         /* bInterval.                       */
};

/*
 * Configuration Descriptor wrapper.
 */
static const USBDescriptor vcom_configuration_descriptor = {
    sizeof vcom_configuration_descriptor_data, vcom_configuration_descriptor_data};

/*
 * U.S. English language identifier.
 */
static const uint8_t vcom_string0[] = {
    USB_DESC_BYTE(4),                     /* bLength.                         */
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType.                 */
    USB_DESC_WORD(0x0409)                 /* wLANGID (U.S. English).          */
};

/*
 * Vendor string.
 */
static const uint8_t vcom_string1[] = {USB_DESC_BYTE(24), /* bLength.                         */
                                       USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType. */
                                       'n',
                                       0,
                                       'a',
                                       0,
                                       'n',
                                       0,
                                       'o',
                                       0,
                                       'v',
                                       0,
                                       'n',
                                       0,
                                       'a',
                                       0,
                                       '.',
                                       0,
                                       'c',
                                       0,
                                       'o',
                                       0,
                                       'm',
                                       0};

/*
 * Device Description string.
 */
static const uint8_t vcom_string2[] = {
#if defined(NANOVNA_F303)
    USB_DESC_BYTE(22),                    /* bLength.                         */
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType.                 */
    'N',
    0,
    'a',
    0,
    'n',
    0,
    'o',
    0,
    'V',
    0,
    'N',
    0,
    'A',
    0,
    '-',
    0,
    'H',
    0,
    '4',
    0
#else
    USB_DESC_BYTE(20),                    /* bLength.                         */
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType.                 */
    'N',
    0,
    'a',
    0,
    'n',
    0,
    'o',
    0,
    'V',
    0,
    'N',
    0,
    'A',
    0,
    '-',
    0,
    'H',
    0,
#endif
};

/*
 * Serial Number string.
 */
static const uint8_t vcom_string3[] = {USB_DESC_BYTE(8), /* bLength.                         */
                                       USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType. */
                                       '0' + CH_KERNEL_MAJOR,
                                       0,
                                       '0' + CH_KERNEL_MINOR,
                                       0,
                                       '0' + CH_KERNEL_PATCH,
                                       0};

/*
 * Strings wrappers array.
 */
static const USBDescriptor vcom_strings[] = {
    [STR_LANG_ID] = {sizeof vcom_string0, vcom_string0},
    [STR_MANUFACTURER] = {sizeof vcom_string1, vcom_string1},
    [STR_PRODUCT] = {sizeof vcom_string2, vcom_string2},
    [STR_SERIAL] = {sizeof vcom_string3, vcom_string3}};

#ifdef __USB_UID__
// Use unique serial string generated from MCU id
#define UID_RADIX 5 // Radix conversion constant (5 bit, use 0..9 and A..V)
#define USB_SERIAL_STRING_SIZE (64 / UID_RADIX) // Result string size

static const USBDescriptor* get_serial_string_descriptor(void) {
  static USBDescriptor descriptor;
  static uint8_t serial_string[(USB_SERIAL_STRING_SIZE + 1) * sizeof(uint16_t)] = {0};

  if (descriptor.ud_size == 0U) {
    const uint32_t id0 = *(uint32_t*)0x1FFFF7AC; // MCU id0 address
    const uint32_t id1 = *(uint32_t*)0x1FFFF7B0; // MCU id1 address
    const uint32_t id2 = *(uint32_t*)0x1FFFF7B4; // MCU id2 address
    uint64_t uid = id1;
    uid |= ((uint64_t)(id0 + id2)) | (uid << 32); // generate unique 64bit ID

    serial_string[0] = (uint8_t)sizeof(serial_string);
    serial_string[1] = USB_DESCRIPTOR_STRING;

    for (uint32_t i = 0; i < USB_SERIAL_STRING_SIZE; i++) {
      const uint16_t c = uid & ((1U << UID_RADIX) - 1U);
      serial_string[2 + (i * 2)] = (uint8_t)(c < 0x0A ? ('0' + c) : ('A' + c - 0x0A));
      serial_string[3 + (i * 2)] = 0;
      uid >>= UID_RADIX;
    }

    descriptor.ud_size = sizeof(serial_string);
    descriptor.ud_string = serial_string;
  }

  return &descriptor;
}
#endif

/*
 * Handles the GET_DESCRIPTOR callback. All required descriptors must be
 * handled here.
 */
static const USBDescriptor* get_descriptor(USBDriver* usbp, uint8_t dtype, uint8_t dindex,
                                           uint16_t lang) {

  (void)usbp;
  (void)lang;
  switch (dtype) {
  case USB_DESCRIPTOR_DEVICE:
    return &vcom_device_descriptor;
  case USB_DESCRIPTOR_CONFIGURATION:
    return &vcom_configuration_descriptor;
  case USB_DESCRIPTOR_STRING:
#ifdef __USB_UID__ // send unique USB serial string if need
    if (dindex == STR_SERIAL && VNA_MODE(VNA_MODE_USB_UID))
      return get_serial_string_descriptor();
#endif
    if (dindex < 4)
      return &vcom_strings[dindex];
  }
  return NULL;
}

/**
 * @brief   IN EP1 state.
 */
static USBInEndpointState ep1instate;

/**
 * @brief   OUT EP1 state.
 */
static USBOutEndpointState ep1outstate;

/**
 * @brief   EP1 initialization structure (both IN and OUT).
 */
static const USBEndpointConfig ep1config = {USB_EP_MODE_TYPE_BULK, NULL,        sduDataTransmitted,
                                            sduDataReceived,       0x0040,      0x0040,
                                            &ep1instate,           &ep1outstate};

/**
 * @brief   IN EP2 state.
 */
static USBInEndpointState ep2instate;

/**
 * @brief   EP2 initialization structure (IN only).
 */
static const USBEndpointConfig ep2config = {
    USB_EP_MODE_TYPE_INTR, NULL, sduInterruptTransmitted, NULL, 0x0010, 0x0000, &ep2instate, NULL};

/*
 * Handles the USB driver global events.
 */
static void usb_event(USBDriver* usbp, usbevent_t event) {
  extern SerialUSBDriver SDU1;
  chSysLockFromISR();
  switch (event) {
  case USB_EVENT_RESET:
    break;
  case USB_EVENT_ADDRESS:
    break;
  case USB_EVENT_CONFIGURED:
    /* Enables the endpoints specified into the configuration.
       Note, this callback is invoked from an ISR so I-Class functions
       must be used.*/
    usbInitEndpointI(usbp, USBD1_DATA_REQUEST_EP, &ep1config);
    usbInitEndpointI(usbp, USBD1_INTERRUPT_REQUEST_EP, &ep2config);
    /* Resetting the state of the CDC subsystem.*/
    sduConfigureHookI(&SDU1);
    break;
  case USB_EVENT_SUSPEND:
    /* Disconnection event on suspend.*/
    sduDisconnectI(&SDU1);
    /* Wake up any waiting shell threads to prevent hanging */
    shell_wake_all_waiting_threads();
    /* Update connection state to disconnected */
    shell_update_vcp_connection_state(false);
    break;
  case USB_EVENT_WAKEUP:
    break;
  case USB_EVENT_STALLED:
    break;
  }
  chSysUnlockFromISR();
  return;
}

/*
 * Handles the USB driver global events.
 */
static void sof_handler(USBDriver* usbp) {
  (void)usbp;
  osalSysLockFromISR();
  sduSOFHookI(&SDU1);
  osalSysUnlockFromISR();
}

/*
 * Custom hook to handle CDC requests including line state changes.
 */
bool custom_sduRequestsHook(USBDriver *usbp) {

  if ((usbp->setup.bmRequestType & USB_RTYPE_TYPE_MASK) == USB_RTYPE_TYPE_CLASS) {
    switch (usbp->setup.bRequest) {
    case CDC_GET_LINE_CODING:
      usbSetupTransfer(usbp, (uint8_t *)&linecoding, sizeof(linecoding), NULL);
      return true;
    case CDC_SET_LINE_CODING:
      usbSetupTransfer(usbp, (uint8_t *)&linecoding, sizeof(linecoding), NULL);
      return true;
    case CDC_SET_CONTROL_LINE_STATE:
      /* Check the DTR state - bit 0 of wValue */
      if ((usbp->setup.wValue & 1) != 0) {
        /* DTR is asserted - host has opened the port */
        sduConfigureHookI(&SDU1);
        shell_update_vcp_connection_state(true);
      } else {
        /* DTR is not asserted - host has closed the port */
        sduDisconnectI(&SDU1);
        /* Wake up any waiting shell threads to prevent hanging */
        shell_wake_all_waiting_threads();
        shell_update_vcp_connection_state(false);
      }
      usbSetupTransfer(usbp, NULL, 0, NULL);
      return true;
    default:
      return false;
    }
  }
  return false;
}

/*
 * USB driver configuration.
 */
const USBConfig usbcfg = {usb_event, get_descriptor, custom_sduRequestsHook, sof_handler};

/*
 * Serial over USB driver configuration.
 */
const SerialUSBConfig serusbcfg = {&USBD1, USBD1_DATA_REQUEST_EP, USBD1_DATA_AVAILABLE_EP,
                                   USBD1_INTERRUPT_REQUEST_EP};
