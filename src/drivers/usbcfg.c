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

#include <stddef.h>

/* Virtual serial port over USB.*/
SerialUSBDriver SDU1;

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
    /* CDC Notification EP: 16 bytes, 16 ms polling. */
    USB_DESC_ENDPOINT(USBD1_INTERRUPT_REQUEST_EP | 0x80, 0x03, /* bmAttributes (Interrupt). */
                      0x0010, /* wMaxPacketSize.                  */
                      0x10),  /* bInterval.                       */
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
 * Strings wrappers array.
 */
static const USBDescriptor vcom_strings[] = {
    [STR_LANG_ID] = {sizeof vcom_string0, vcom_string0},
    [STR_MANUFACTURER] = {sizeof vcom_string1, vcom_string1},
    [STR_PRODUCT] = {sizeof vcom_string2, vcom_string2}};

#define SERIAL_STRING_CHARS 24U
static USBDescriptor serial_string_descriptor;
static uint8_t serial_string_buffer[2U + (SERIAL_STRING_CHARS * 2U)];
static bool serial_string_initialized = false;

static const USBDescriptor* get_serial_string_descriptor(void) {
  if (!serial_string_initialized) {
    size_t index = 2U;
    serial_string_buffer[1] = USB_DESCRIPTOR_STRING;
#if defined(__USB_UID__)
    const uint32_t* uid_words = (const uint32_t*)UID_BASE;
    for (int word = 2; word >= 0; --word) {
      uint32_t value = uid_words[word];
      for (int nibble = 0; nibble < 8; ++nibble) {
        uint32_t digit = (value >> 28) & 0x0FU;
        value <<= 4;
        char c = (digit < 10U) ? (char)('0' + digit) : (char)('A' + (digit - 10U));
        serial_string_buffer[index++] = (uint8_t)c;
        serial_string_buffer[index++] = 0U;
      }
    }
#else
    static const char fallback_serial[] = "000000000000000000000000";
    for (size_t i = 0; i < sizeof fallback_serial - 1U; ++i) {
      serial_string_buffer[index++] = (uint8_t)fallback_serial[i];
      serial_string_buffer[index++] = 0U;
    }
#endif
    serial_string_buffer[0] = (uint8_t)index;
    serial_string_descriptor.ud_size = (uint16_t)index;
    serial_string_descriptor.ud_string = serial_string_buffer;
    serial_string_initialized = true;
  }
  return &serial_string_descriptor;
}

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
    if (dindex == STR_SERIAL) {
      return get_serial_string_descriptor();
    }
    if (dindex < ARRAY_COUNT(vcom_strings))
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
static bool usb_endpoints_configured = false;
static thread_reference_t sdu_configured_tr = NULL;

bool usbWaitSerialConfiguredTimeout(systime_t timeout) {
  osalSysLock();
  if (usb_endpoints_configured) {
    osalSysUnlock();
    return true;
  }
  msg_t msg = osalThreadSuspendTimeoutS(&sdu_configured_tr, timeout);
  osalSysUnlock();
  return msg == MSG_OK;
}

/* Minimal and safe USB events handling. */
static void usbevent(USBDriver* usbp, usbevent_t event) {
  osalSysLockFromISR();
  switch (event) {
  case USB_EVENT_RESET:
    usb_endpoints_configured = false;
    if (sdu_configured_tr != NULL) {
      osalThreadResumeI(&sdu_configured_tr, MSG_RESET);
    }
    break;
  case USB_EVENT_CONFIGURED:
    usbInitEndpointI(usbp, USBD1_DATA_REQUEST_EP, &ep1config);
    usbInitEndpointI(usbp, USBD1_INTERRUPT_REQUEST_EP, &ep2config);
    sduConfigureHookI(&SDU1);
    usb_endpoints_configured = true;
    if (sdu_configured_tr != NULL) {
      osalThreadResumeI(&sdu_configured_tr, MSG_OK);
    }
    break;
  case USB_EVENT_SUSPEND:
    break;
  }
  osalSysUnlockFromISR();
}

/* Temporary SOF hook for debugging USB clock/SOF presence. */
static void sofhandler(USBDriver* usbp) {
  (void)usbp;
#if defined(GPIOC) && defined(GPIOC_LED)
  GPIOC->ODR ^= (1U << GPIOC_LED);
#endif
  osalSysLockFromISR();
  sduSOFHookI(&SDU1);
  osalSysUnlockFromISR();
}

/*
 * USB driver configuration.
 */
const USBConfig usbcfg = {usbevent, get_descriptor, sduRequestsHook, sofhandler};

/*
 * Serial over USB driver configuration.
 */
const SerialUSBConfig serusbcfg = {&USBD1, USBD1_DATA_REQUEST_EP, USBD1_DATA_AVAILABLE_EP,
                                   USBD1_INTERRUPT_REQUEST_EP};
