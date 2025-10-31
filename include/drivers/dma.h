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

#include "ch.h"
#include "hal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  DMA_Channel_TypeDef* channel;
  const volatile void* peripheral;
  uint32_t base_config;
} dma_channel_t;

void dma_channel_init(dma_channel_t* handle, DMA_Channel_TypeDef* channel,
                      const volatile void* peripheral, uint32_t base_config);
void dma_channel_start(dma_channel_t* handle, const void* memory, uint16_t length,
                       uint32_t extra_config);
void dma_channel_wait(dma_channel_t* handle);
void dma_channel_abort(dma_channel_t* handle);
uint16_t dma_channel_get_remaining(const dma_channel_t* handle);
bool dma_channel_is_active(const dma_channel_t* handle);

