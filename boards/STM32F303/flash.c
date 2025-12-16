#include "hal.h"
#include "ch.h"

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

static inline int flash_wait_for_last_operation(void) {
  // Add a timeout counter to prevent infinite loop
  volatile uint32_t timeout = 0x100000;  // Increased timeout for reliability
  
  while ((FLASH->SR & FLASH_SR_BSY) && (timeout > 0)) {
    timeout--;
  }
  
  // Check if we timed out
  if (timeout == 0) {
    // Clear the busy flag by software reset if stuck
    FLASH->SR |= FLASH_SR_EOP;  // Clear EOP if set
    return -1;  // Return error
  }
  
  // Check for error flags
  uint32_t error_flags = 0;
#if defined(FLASH_SR_WRPRTERR)
  error_flags |= FLASH_SR_WRPRTERR;
#elif defined(FLASH_SR_WRPERR)
  error_flags |= FLASH_SR_WRPERR;
#endif
#ifdef FLASH_SR_PGERR
  error_flags |= FLASH_SR_PGERR;
#endif
#ifdef FLASH_SR_PGAERR
  error_flags |= FLASH_SR_PGAERR;
#endif
#ifdef FLASH_SR_PGPERR
  error_flags |= FLASH_SR_PGPERR;
#endif
#ifdef FLASH_SR_PGSERR
  error_flags |= FLASH_SR_PGSERR;
#endif
#ifdef FLASH_SR_OPERR
  error_flags |= FLASH_SR_OPERR;
#endif
  
  if (FLASH->SR & error_flags) {
    flash_clear_status_flags();
    return -1;  // Return error
  }
  
  flash_clear_status_flags();
  return 0;  // Success
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
  if (flash_wait_for_last_operation() != 0) {
    return;  // Return if previous operation failed
  }
  FLASH->CR |= FLASH_CR_PER;
  FLASH->AR = page_address;
  FLASH->CR |= FLASH_CR_STRT;
  
  // Wait for operation with timeout
  if (flash_wait_for_last_operation() != 0) {
    // If timeout occurred, try to clear the error
    FLASH->CR &= ~FLASH_CR_PER;
    return;
  }
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
  flash_exit_critical(primask);
  
  __IO uint16_t* p = dst;
  for (uint32_t i = 0; i < size / sizeof(uint16_t); i++) {
    primask = flash_enter_critical();
    if (flash_wait_for_last_operation() != 0) {
      flash_exit_critical(primask);
      // If waiting fails, try to continue with the next word to avoid hanging
      continue;
    }
    FLASH->CR |= FLASH_CR_PG;
    p[i] = data[i];
    if (flash_wait_for_last_operation() != 0) {
      // If programming failed, continue to avoid indefinite hanging
      FLASH->CR &= ~FLASH_CR_PG;
      flash_exit_critical(primask);
      continue;
    }
    FLASH->CR &= ~FLASH_CR_PG;
    flash_exit_critical(primask);
    
    // Yield periodically to keep system responsive during large flash operations
    if ((i & 0xFF) == 0) {  // yield every 256 half-words (~512 bytes)
      chThdYield();
    }
  }
  
  // Finally, lock the flash again
  primask = flash_enter_critical();
  flash_lock();
  flash_exit_critical(primask);
}
