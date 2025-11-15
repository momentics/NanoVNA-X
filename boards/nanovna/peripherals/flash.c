/* Auto-generated unified peripheral module */
#if defined(NANOVNA_F303)
#include "hal.h"

static inline void flash_clear_status_flags(void) {
  uint32_t flags = FLASH_SR_EOP;
#if defined(FLASH_SR_WRPRTERR)
  flags |= FLASH_SR_WRPRTERR;
#elif defined(FLASH_SR_WRPERR)
  flags |= FLASH_SR_WRPERR;
#endif
#ifdef FLASH_SR_PGERR
  flags |= FLASH_SR_PGERR;
#endif
#ifdef FLASH_SR_PGAERR
  flags |= FLASH_SR_PGAERR;
#endif
#ifdef FLASH_SR_PGPERR
  flags |= FLASH_SR_PGPERR;
#endif
#ifdef FLASH_SR_PGSERR
  flags |= FLASH_SR_PGSERR;
#endif
#ifdef FLASH_SR_OPERR
  flags |= FLASH_SR_OPERR;
#endif
  FLASH->SR = flags;
}

static inline void flash_wait_for_last_operation(void) {
  while (FLASH->SR & FLASH_SR_BSY) {
    // wait
  }
  flash_clear_status_flags();
}

static inline uint32_t flash_enter_critical(void) {
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static inline void flash_exit_critical(uint32_t primask) {
  if ((primask & 1U) == 0U) {
    __enable_irq();
  }
}

static void flash_lock(void) {
  FLASH->CR |= FLASH_CR_LOCK;
}

static void flash_erase_page0(uint32_t page_address) {
  flash_wait_for_last_operation();
  FLASH->CR |= FLASH_CR_PER;
  FLASH->AR = page_address;
  FLASH->CR |= FLASH_CR_STRT;
  flash_wait_for_last_operation();
  FLASH->CR &= ~FLASH_CR_PER;
}

static inline void flash_unlock(void) {
  // unlock sequence
  FLASH->KEYR = FLASH_KEY1;
  FLASH->KEYR = FLASH_KEY2;
}

static void flash_erase_pages_unlocked(uint32_t page_address, uint32_t size) {
  size += page_address;
  for (; page_address < size; page_address += FLASH_PAGESIZE)
    flash_erase_page0(page_address);
}

void flash_erase_pages(uint32_t page_address, uint32_t size) {
  uint32_t primask = flash_enter_critical();
  flash_unlock();
  flash_erase_pages_unlocked(page_address, size);
  flash_lock();
  flash_exit_critical(primask);
}

void flash_program_half_word_buffer(uint16_t* dst, uint16_t *data, uint16_t size) {
  uint32_t primask = flash_enter_critical();
  flash_unlock();
  flash_erase_pages_unlocked((uint32_t)dst, size);
  __IO uint16_t* p = dst;
  for (uint32_t i = 0; i < size / sizeof(uint16_t); i++) {
    flash_wait_for_last_operation();
    FLASH->CR |= FLASH_CR_PG;
    p[i] = data[i];
    flash_wait_for_last_operation();
    FLASH->CR &= ~FLASH_CR_PG;
  }
  flash_lock();
  flash_exit_critical(primask);
}
#else /* STM32F072 */
/*
 * Copyright (c) 2019-2020, Dmitry (DiSlord) dislordlive@gmail.com
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

#include "hal.h"

static inline void flash_clear_status_flags(void) {
  uint32_t flags = FLASH_SR_EOP;
#if defined(FLASH_SR_WRPRTERR)
  flags |= FLASH_SR_WRPRTERR;
#elif defined(FLASH_SR_WRPERR)
  flags |= FLASH_SR_WRPERR;
#endif
#ifdef FLASH_SR_PGERR
  flags |= FLASH_SR_PGERR;
#endif
#ifdef FLASH_SR_OPERR
  flags |= FLASH_SR_OPERR;
#endif
  FLASH->SR = flags;
}

static inline void flash_wait_for_last_operation(void) {
  while (FLASH->SR & FLASH_SR_BSY) {
    // wait
  }
  flash_clear_status_flags();
}

static inline uint32_t flash_enter_critical(void) {
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static inline void flash_exit_critical(uint32_t primask) {
  if ((primask & 1U) == 0U) {
    __enable_irq();
  }
}

static inline void flash_lock(void) {
  FLASH->CR |= FLASH_CR_LOCK;
}

static void flash_erase_page0(uint32_t page_address) {
  flash_wait_for_last_operation();
  FLASH->CR |= FLASH_CR_PER;
  FLASH->AR = page_address;
  FLASH->CR |= FLASH_CR_STRT;
  flash_wait_for_last_operation();
  FLASH->CR &= ~FLASH_CR_PER;
}

static inline void flash_unlock(void) {
  // unlock sequence
  FLASH->KEYR = FLASH_KEY1;
  FLASH->KEYR = FLASH_KEY2;
}

static void flash_erase_pages_unlocked(uint32_t page_address, uint32_t size) {
  size += page_address;
  for (; page_address < size; page_address += FLASH_PAGESIZE)
    flash_erase_page0(page_address);
}

void flash_erase_pages(uint32_t page_address, uint32_t size) {
  uint32_t primask = flash_enter_critical();
  flash_unlock();
  flash_erase_pages_unlocked(page_address, size);
  flash_lock();
  flash_exit_critical(primask);
}

void flash_program_half_word_buffer(uint16_t* dst, uint16_t *data, uint16_t size) {
  uint32_t primask = flash_enter_critical();
  flash_unlock();
  flash_erase_pages_unlocked((uint32_t)dst, size);
  __IO uint16_t* p = dst;
  for (uint32_t i = 0; i < size / sizeof(uint16_t); i++) {
    flash_wait_for_last_operation();
    FLASH->CR |= FLASH_CR_PG;
    p[i] = data[i];
    flash_wait_for_last_operation();
    FLASH->CR &= ~FLASH_CR_PG;
  }
  flash_lock();
  flash_exit_critical(primask);
}
#endif /* NANOVNA_F303 */
