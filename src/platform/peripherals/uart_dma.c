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

#include "platform/peripherals/uart_dma.h"

#if HAL_USE_UART == TRUE

static size_t uart_dma_send_buffer(UARTDriver* driver, const uint8_t* buffer, size_t size,
                                   systime_t timeout, msg_t* last_status) {
  size_t transmitted = 0;
  *last_status = MSG_OK;
  while (transmitted < size) {
    size_t chunk = size - transmitted;
    if (chunk > 0xFFFFU) {
      chunk = 0xFFFFU;
    }
    size_t frames = chunk;
    msg_t status = uartSendFullTimeout(driver, &frames, buffer + transmitted, timeout);
    if (status != MSG_OK) {
      transmitted += chunk - frames;
      *last_status = status;
      return transmitted;
    }
    transmitted += chunk;
  }
  return transmitted;
}

static size_t uart_dma_receive_buffer(UARTDriver* driver, uint8_t* buffer, size_t size,
                                      systime_t timeout, msg_t* last_status) {
  size_t received = 0;
  *last_status = MSG_OK;
  while (received < size) {
    size_t chunk = size - received;
    if (chunk > 0xFFFFU) {
      chunk = 0xFFFFU;
    }
    size_t frames = chunk;
    msg_t status = uartReceiveTimeout(driver, &frames, buffer + received, timeout);
    if (status != MSG_OK) {
      received += chunk - frames;
      *last_status = status;
      return received;
    }
    received += chunk;
  }
  return received;
}

static size_t uart_stream_write(void* instance, const uint8_t* buffer, size_t size) {
  if (size == 0U) {
    return 0;
  }
  UARTDriver* driver = (UARTDriver*)instance;
  msg_t status;
  size_t written = uart_dma_send_buffer(driver, buffer, size, TIME_INFINITE, &status);
  (void)status;
  return written;
}

static size_t uart_stream_read(void* instance, uint8_t* buffer, size_t size) {
  if (size == 0U) {
    return 0;
  }
  UARTDriver* driver = (UARTDriver*)instance;
  msg_t status;
  size_t read = uart_dma_receive_buffer(driver, buffer, size, TIME_INFINITE, &status);
  (void)status;
  return read;
}

static msg_t uart_stream_put(void* instance, uint8_t value) {
  UARTDriver* driver = (UARTDriver*)instance;
  msg_t status;
  (void)uart_dma_send_buffer(driver, &value, 1U, TIME_INFINITE, &status);
  return status;
}

static msg_t uart_stream_get(void* instance) {
  UARTDriver* driver = (UARTDriver*)instance;
  uint8_t value = 0U;
  msg_t status;
  size_t received = uart_dma_receive_buffer(driver, &value, 1U, TIME_INFINITE, &status);
  if ((status == MSG_OK) && (received == 1U)) {
    return (msg_t)value;
  }
  return status;
}

static const struct BaseSequentialStreamVMT uart_stream_vmt = {
    uart_stream_write,
    uart_stream_read,
    uart_stream_put,
    uart_stream_get,
};

static UARTConfig uart_config = {
    .txend1_cb = NULL,
    .txend2_cb = NULL,
    .rxend_cb = NULL,
    .rxchar_cb = NULL,
    .rxerr_cb = NULL,
    .speed = 115200,
    .cr1 = 0,
    .cr2 = 0U,
    .cr3 = 0,
};

static UARTDriver* uart_driver = &UARTD1;
static BaseSequentialStream uart_stream = {
    .vmt = &uart_stream_vmt,
};

BaseSequentialStream* uart_dma_stream(void) {
  return &uart_stream;
}

static void uart_dma_restart(uint32_t baudrate) {
  uart_config.speed = baudrate;
  if (uart_driver->state == UART_READY) {
    uartStop(uart_driver);
  }
  uartStart(uart_driver, &uart_config);
}

void uart_dma_init(uint32_t baudrate) {
  uart_dma_restart(baudrate);
}

void uart_dma_set_baudrate(uint32_t baudrate) {
  uart_dma_restart(baudrate);
}

void uart_dma_stop(void) {
  if (uart_driver->state == UART_READY) {
    uartStop(uart_driver);
  }
}

void uart_dma_flush_queues(void) {
  if (uart_driver->state != UART_READY) {
    return;
  }
  (void)uartStopSend(uart_driver);
  (void)uartStopReceive(uart_driver);
}

size_t uart_dma_write_timeout(const uint8_t* data, size_t size, systime_t timeout) {
  if ((uart_driver->state != UART_READY) || (size == 0U)) {
    return 0;
  }
  msg_t status;
  return uart_dma_send_buffer(uart_driver, data, size, timeout, &status);
}

size_t uart_dma_read_timeout(uint8_t* data, size_t size, systime_t timeout) {
  if ((uart_driver->state != UART_READY) || (size == 0U)) {
    return 0;
  }
  msg_t status;
  return uart_dma_receive_buffer(uart_driver, data, size, timeout, &status);
}

msg_t uart_dma_put_timeout(uint8_t value, systime_t timeout) {
  if (uart_driver->state != UART_READY) {
    return MSG_RESET;
  }
  msg_t status;
  (void)uart_dma_send_buffer(uart_driver, &value, 1U, timeout, &status);
  return status;
}

msg_t uart_dma_get_timeout(uint8_t* value, systime_t timeout) {
  if ((uart_driver->state != UART_READY) || (value == NULL)) {
    return MSG_RESET;
  }
  msg_t status;
  size_t received = uart_dma_receive_buffer(uart_driver, value, 1U, timeout, &status);
  if ((status == MSG_OK) && (received == 1U)) {
    return MSG_OK;
  }
  return status;
}

#else /* HAL_USE_UART == TRUE */

static SerialConfig serial_config = {
    .speed = 115200,
    .cr1 = 0,
    .cr2 = USART_CR2_STOP1_BITS,
    .cr3 = 0,
};

static SerialDriver* serial_driver = &SD1;

BaseSequentialStream* uart_dma_stream(void) {
  return (BaseSequentialStream*)serial_driver;
}

static void serial_restart(uint32_t baudrate) {
  serial_config.speed = baudrate;
  if (serial_driver->state == SD_READY) {
    sdStop(serial_driver);
  }
  sdStart(serial_driver, &serial_config);
}

void uart_dma_init(uint32_t baudrate) {
  serial_restart(baudrate);
}

void uart_dma_set_baudrate(uint32_t baudrate) {
  if (serial_driver->state == SD_READY) {
    sdSetBaudrate(serial_driver, baudrate);
  } else {
    serial_restart(baudrate);
  }
}

void uart_dma_stop(void) {
  if (serial_driver->state == SD_READY) {
    sdStop(serial_driver);
  }
}

void uart_dma_flush_queues(void) {
  if (serial_driver->state != SD_READY) {
    return;
  }
  osalSysLock();
  qResetI(&serial_driver->oqueue);
  qResetI(&serial_driver->iqueue);
  osalSysUnlock();
}

size_t uart_dma_write_timeout(const uint8_t* data, size_t size, systime_t timeout) {
  if ((serial_driver->state != SD_READY) || (data == NULL) || (size == 0U)) {
    return 0;
  }
  return sdWriteTimeout(serial_driver, data, size, timeout);
}

size_t uart_dma_read_timeout(uint8_t* data, size_t size, systime_t timeout) {
  if ((serial_driver->state != SD_READY) || (data == NULL) || (size == 0U)) {
    return 0;
  }
  return sdReadTimeout(serial_driver, data, size, timeout);
}

msg_t uart_dma_put_timeout(uint8_t value, systime_t timeout) {
  if (serial_driver->state != SD_READY) {
    return MSG_RESET;
  }
  return sdPutTimeout(serial_driver, value, timeout);
}

msg_t uart_dma_get_timeout(uint8_t* value, systime_t timeout) {
  if ((serial_driver->state != SD_READY) || (value == NULL)) {
    return MSG_RESET;
  }
  msg_t status = sdGetTimeout(serial_driver, timeout);
  if (status < MSG_OK) {
    return status;
  }
  *value = (uint8_t)status;
  return MSG_OK;
}

#endif /* HAL_USE_UART == TRUE */
