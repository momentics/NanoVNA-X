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
#include "ch.h"
#include "hal.h"
#include "nanovna.h"
#include "infra/storage/config_service.h"
#include "infra/event/event_bus.h"

#include <stdbool.h>
#include <string.h>

// Semaphore to protect flash operations from concurrent access
static semaphore_t flash_operation_semaphore;



uint16_t lastsaveid = 0;
#if SAVEAREA_MAX >= 8
#error "Increase checksum_ok type for save more cache slots"
#endif

// properties CRC check cache (max 8 slots)
static uint8_t checksum_ok = 0;
static event_bus_t* config_event_bus = NULL;

static void config_on_configuration_changed(const event_bus_message_t* message, void* user_data) {
  (void)user_data;
  if (message == NULL) {
    return;
  }
  if (message->topic != EVENT_CONFIGURATION_CHANGED) {
    return;
  }
  (void)config_save();
}

static uint32_t calibration_slot_area(int id) {
  return SAVE_PROP_CONFIG_ADDR + id * SAVE_PROP_CONFIG_SIZE;
}

static uint32_t checksum(const void* start, size_t len) {
  uint32_t* p = (uint32_t*)start;
  uint32_t value = 0;
  // align by sizeof(uint32_t)
  len = (len + sizeof(uint32_t) - 1) / sizeof(uint32_t);
  while (len-- > 0)
    value = __ROR(value, 31) + *p++;
  return value;
}

static int config_save_impl(void) {
  // Wait for exclusive access to flash operations with timeout to prevent blocking measurements  
  msg_t msg = MSG_OK;
  // During calibration, don't wait for semaphore as it could delay critical measurements
  if (calibration_in_progress > 0) {
    msg = chSemWaitTimeout(&flash_operation_semaphore, MS2ST(100)); // Very short timeout during calibration
  } else {
    msg = chSemWaitTimeout(&flash_operation_semaphore, MS2ST(500)); // 500ms timeout normally
  }
  
  // If we can't get the semaphore within timeout, return error
  if (msg != MSG_OK) {
    return -1;  // Failed to get access to flash
  }
  
  // Apply magic word and calculate checksum
  config.magic = CONFIG_MAGIC;
  config.checksum = checksum(&config, sizeof config - sizeof config.checksum);

  // write to flash
  flash_program_half_word_buffer((uint16_t*)SAVE_CONFIG_ADDR, (uint16_t*)&config, sizeof(config_t));
  
  // Release the semaphore
  chSemSignal(&flash_operation_semaphore);
  
  if (config_event_bus != NULL) {
    event_bus_publish(config_event_bus, EVENT_STORAGE_UPDATED, NULL);
  }
  return 0;
}

static int config_recall_impl(void) {
  const config_t* src = (const config_t*)SAVE_CONFIG_ADDR;

  if (src->magic != CONFIG_MAGIC ||
      checksum(src, sizeof *src - sizeof src->checksum) != src->checksum)
    return -1;
  // duplicated saved data onto sram to be able to modify marker/trace
  memcpy(&config, src, sizeof(config_t));
  return 0;
}

static int caldata_save_impl(uint32_t id) {
  if (id >= SAVEAREA_MAX)
    return -1;

  // Don't save during critical calibration phase to prevent data corruption
  if (calibration_in_progress) {
    return -1;  // Return error to indicate save was denied
  }

  // Wait for exclusive access to flash operations
  msg_t msg = chSemWaitTimeout(&flash_operation_semaphore, MS2ST(500));
  if (msg != MSG_OK)
    return -1;

  // Apply magic word and calculate checksum
  current_props.magic = PROPERTIES_MAGIC;
  current_props.checksum =
      checksum(&current_props, sizeof current_props - sizeof current_props.checksum);

  // write to flash
  uint16_t* dst = (uint16_t*)calibration_slot_area(id);
  flash_program_half_word_buffer(dst, (uint16_t*)&current_props, sizeof(properties_t));

  lastsaveid = id;
  
  chSemSignal(&flash_operation_semaphore);
  return 0;
}

const properties_t* get_properties(uint32_t id) {
  if (id >= SAVEAREA_MAX)
    return NULL;
  // point to saved area on the flash memory
  properties_t* src = (properties_t*)calibration_slot_area(id);
  // Check crc cache mask (made it only 1 time)
  if (checksum_ok & (1 << id))
    return src;
  if (src->magic != PROPERTIES_MAGIC ||
      checksum(src, sizeof *src - sizeof src->checksum) != src->checksum)
    return NULL;
  checksum_ok |= 1 << id;
  return src;
}

static int caldata_recall_impl(uint32_t id) {
  lastsaveid = NO_SAVE_SLOT;
  if (id == NO_SAVE_SLOT)
    return 0;

  // Wait for exclusive access to flash operations (read consistency)
  msg_t msg = chSemWaitTimeout(&flash_operation_semaphore, MS2ST(500));
  if (msg != MSG_OK)
    return -1;

  // point to saved area on the flash memory
  const properties_t* src = get_properties(id);
  if (src == NULL) {
    chSemSignal(&flash_operation_semaphore);
    //  load_default_properties();
    return 1;
  }
  // active configuration points to save data on flash memory
  lastsaveid = id;
  // duplicated saved data onto sram to be able to modify marker/trace
  memcpy(&current_props, src, sizeof(properties_t));
  
  chSemSignal(&flash_operation_semaphore);
  return 0;
}

static void clear_all_config_prop_data_impl(void) {
  // Wait for exclusive access to flash operations with timeout
  msg_t msg = chSemWaitTimeout(&flash_operation_semaphore, MS2ST(2000)); // 2 second timeout for erase operation
  
  // If we can't get the semaphore within timeout, return early
  if (msg != MSG_OK) {
    return;  // Failed to get access to flash
  }
  
  lastsaveid = NO_SAVE_SLOT;
  checksum_ok = 0;
  // unlock and erase flash pages
  flash_erase_pages(SAVE_PROP_CONFIG_ADDR, SAVE_FULL_AREA_SIZE);
  
  // Release the semaphore
  chSemSignal(&flash_operation_semaphore);
  
  if (config_event_bus != NULL) {
    event_bus_publish(config_event_bus, EVENT_STORAGE_UPDATED, NULL);
  }
}

static const config_service_api_t api = {
    .save_configuration = config_save_impl,
    .load_configuration = config_recall_impl,
    .save_calibration = caldata_save_impl,
    .load_calibration = caldata_recall_impl,
    .erase_calibration = clear_all_config_prop_data_impl,
};

static bool initialized = false;

static void config_service_init_semaphore(void) {
  chSemObjectInit(&flash_operation_semaphore, 1);  // Initialize with 1 (available)
}

void config_service_init(void) {
  config_service_init_semaphore();
  initialized = true;
}

void config_service_attach_event_bus(event_bus_t* bus) {
  if (config_event_bus == bus) {
    return;
  }
  config_event_bus = bus;
  if (bus != NULL) {
    event_bus_subscribe(bus, EVENT_CONFIGURATION_CHANGED, config_on_configuration_changed, NULL);
  }
}

void config_service_notify_configuration_changed(void) {
  if (config_event_bus != NULL) {
    event_bus_publish(config_event_bus, EVENT_CONFIGURATION_CHANGED, NULL);
  }
}

const config_service_api_t* config_service_api(void) {
  return initialized ? &api : NULL;
}

static const config_service_api_t* require_api(void) {
  const config_service_api_t* instance = config_service_api();
  return instance;
}

int config_save(void) {
  const config_service_api_t* instance = require_api();
  return instance ? instance->save_configuration() : -1;
}

int config_recall(void) {
  const config_service_api_t* instance = require_api();
  return instance ? instance->load_configuration() : -1;
}

int caldata_save(uint32_t id) {
  const config_service_api_t* instance = require_api();
  return instance ? instance->save_calibration(id) : -1;
}

int caldata_recall(uint32_t id) {
  const config_service_api_t* instance = require_api();
  return instance ? instance->load_calibration(id) : -1;
}

void clear_all_config_prop_data(void) {
  const config_service_api_t* instance = require_api();
  if (instance && instance->erase_calibration) {
    instance->erase_calibration();
  }
}
