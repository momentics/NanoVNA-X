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

#include <stdbool.h>
#include <stdint.h>

struct PlatformDrivers;

typedef struct {
  void (*init)(void);
  void (*set_backlight)(uint16_t level);
} display_driver_t;

typedef struct {
  void (*init)(void);
  void (*start_watchdog)(void);
  void (*stop_watchdog)(void);
  uint16_t (*read_channel)(uint32_t channel);
} adc_driver_t;

typedef struct {
  void (*init)(void);
  void (*set_frequency)(uint32_t frequency);
  void (*set_power)(uint16_t value);
} generator_driver_t;

typedef struct {
  void (*init)(void);
  bool (*read)(int16_t *x, int16_t *y);
} touch_driver_t;

typedef struct {
  void (*init)(void);
  void (*program_half_words)(uint16_t *dst, uint16_t *data, uint16_t size);
  void (*erase_pages)(uint32_t address, uint32_t size);
} storage_driver_t;

typedef struct PlatformDrivers {
  void (*init)(void);
  const display_driver_t *display;
  const adc_driver_t *adc;
  const generator_driver_t *generator;
  const touch_driver_t *touch;
  const storage_driver_t *storage;
} PlatformDrivers;

void platform_init(void);
const PlatformDrivers *platform_get_drivers(void);
