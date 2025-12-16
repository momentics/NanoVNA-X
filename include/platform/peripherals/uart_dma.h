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

#pragma once

#include "nanovna.h"

#ifdef __USE_SERIAL_CONSOLE__

#include "ch.h"
#include "hal.h"

#include <stddef.h>
#include <stdint.h>

BaseSequentialStream* uart_dma_stream(void);
void uart_dma_init(uint32_t baudrate);
void uart_dma_set_baudrate(uint32_t baudrate);
void uart_dma_stop(void);
void uart_dma_flush_queues(void);
size_t uart_dma_write_timeout(const uint8_t* data, size_t size, systime_t timeout);
size_t uart_dma_read_timeout(uint8_t* data, size_t size, systime_t timeout);
msg_t uart_dma_put_timeout(uint8_t value, systime_t timeout);
msg_t uart_dma_get_timeout(uint8_t* value, systime_t timeout);

#endif

