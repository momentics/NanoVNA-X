/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com 
 * Based on TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
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

#include "platform/hal.h"

#include "ch.h"
#include "hal.h"

#include "nanovna.h"
#include "si5351.h"

static void board_peripherals_init(void) {
  rccEnableDMA1(false);
#if HAL_USE_PAL == FALSE
  init_pal();
#endif
#ifdef __USE_RTC__
  rtc_init();
#endif
#if defined(__VNA_ENABLE_DAC__) || defined(__LCD_BRIGHTNESS__)
  dac_init();
#endif
  i2c_start();
}

static void display_driver_init(void) {
  lcd_init();
}

static void display_driver_set_backlight(uint16_t level) {
#if defined(__VNA_ENABLE_DAC__) || defined(__LCD_BRIGHTNESS__)
  dac_setvalue_ch2(level);
#else
  (void)level;
#endif
}

static void adc_driver_init(void) {
  adc_init();
}

static void adc_driver_start_watchdog(void) {
  adc_start_analog_watchdog();
}

static void adc_driver_stop_watchdog(void) {
  adc_stop_analog_watchdog();
}

static uint16_t adc_driver_read(uint32_t channel) {
  return adc_single_read(channel);
}

static void generator_driver_init(void) {
  si5351_init();
}

static void generator_driver_set_frequency(uint32_t frequency) {
  si5351_set_frequency(frequency, SI5351_CLK_DRIVE_STRENGTH_AUTO);
}

static void generator_driver_set_power(uint16_t drive_strength) {
  si5351_set_power((uint8_t)drive_strength);
}

static void storage_driver_init(void) {
}

static void storage_driver_program(uint16_t *dst, uint16_t *data, uint16_t size) {
  flash_program_half_word_buffer(dst, data, size);
}

static void storage_driver_erase(uint32_t address, uint32_t size) {
  flash_erase_pages(address, size);
}

static const display_driver_t display_driver = {
  .init = display_driver_init,
  .set_backlight = display_driver_set_backlight,
};

static const adc_driver_t adc_driver = {
  .init = adc_driver_init,
  .start_watchdog = adc_driver_start_watchdog,
  .stop_watchdog = adc_driver_stop_watchdog,
  .read_channel = adc_driver_read,
};

static const generator_driver_t generator_driver = {
  .init = generator_driver_init,
  .set_frequency = generator_driver_set_frequency,
  .set_power = generator_driver_set_power,
};

static const storage_driver_t storage_driver = {
  .init = storage_driver_init,
  .program_half_words = storage_driver_program,
  .erase_pages = storage_driver_erase,
};

static PlatformDrivers drivers = {
  .init = board_peripherals_init,
  .display = &display_driver,
  .adc = &adc_driver,
  .generator = &generator_driver,
  .touch = NULL,
  .storage = &storage_driver,
};

const PlatformDrivers *platform_nanovna_f072_drivers(void) {
  return &drivers;
}

const PlatformDrivers *platform_nanovna_f303_drivers(void) {
  return &drivers;
}
