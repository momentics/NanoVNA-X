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

#include <stdint.h>

/*
 * This stub satisfies the minimal subset of the STM32 HAL interface used by
 * host-side unit tests.  None of the helpers in src/core/common.c rely on real
 * hardware symbols, so the file intentionally stays empty while keeping the
 * include graph identical to the production firmware.
 */

static inline uint16_t __REVSH(uint16_t value) {
  /* Byte-swap implementation for host builds. */
  return (uint16_t)((value << 8) | (value >> 8));
}
