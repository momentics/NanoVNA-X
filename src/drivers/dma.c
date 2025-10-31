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

 #include "drivers/dma.h"

static inline void dma_channel_disable(DMA_Channel_TypeDef* channel) {
  channel->CCR &= ~STM32_DMA_CR_EN;
}

void dma_channel_init(dma_channel_t* handle, DMA_Channel_TypeDef* channel,
                      const volatile void* peripheral, uint32_t base_config) {
  handle->channel = channel;
  handle->peripheral = peripheral;
  handle->base_config = base_config & ~STM32_DMA_CR_EN;

  dma_channel_disable(channel);
  channel->CCR = 0;
  channel->CNDTR = 0;
  channel->CMAR = 0;
  channel->CPAR = (uint32_t)peripheral;
}

bool dma_channel_is_active(const dma_channel_t* handle) {
  return (handle->channel->CCR & STM32_DMA_CR_EN) != 0U;
}

static inline void dma_channel_wait_for_completion(DMA_Channel_TypeDef* channel) {
  while ((channel->CNDTR & 0xFFFFU) != 0U) {
  }
}

void dma_channel_start(dma_channel_t* handle, const void* memory, uint16_t length,
                       uint32_t extra_config) {
  DMA_Channel_TypeDef* channel = handle->channel;

  if ((channel->CCR & STM32_DMA_CR_EN) != 0U) {
    dma_channel_wait_for_completion(channel);
    dma_channel_disable(channel);
  }

  channel->CNDTR = length;
  channel->CMAR = (uint32_t)memory;
  channel->CCR = handle->base_config | (extra_config & ~STM32_DMA_CR_EN) | STM32_DMA_CR_EN;
}

void dma_channel_wait(dma_channel_t* handle) {
  DMA_Channel_TypeDef* channel = handle->channel;
  dma_channel_wait_for_completion(channel);
  dma_channel_disable(channel);
  channel->CCR = handle->base_config;
}

void dma_channel_abort(dma_channel_t* handle) {
  DMA_Channel_TypeDef* channel = handle->channel;
  dma_channel_disable(channel);
  channel->CNDTR = 0;
  channel->CCR = handle->base_config;
}

uint16_t dma_channel_get_remaining(const dma_channel_t* handle) {
  return (uint16_t)(handle->channel->CNDTR & 0xFFFFU);
}

