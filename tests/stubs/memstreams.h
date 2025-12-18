#pragma once

#include <stdint.h>
#include <stddef.h>
#include "chprintf.h"

/* Minimal MemoryStream definition for host tests */
/* Must match the usage in shell_service.c */

typedef struct {
    /* Mimic BaseSequentialStream inheritance/structure if necessary 
       but for the test's custom chvprintf we just need a common base or 
       we cast pointers. 
       In ChibiOS: struct MemoryStream { const struct MemStreamVMT *vmt; ... };
       We will use a placeholder for vmt to align with standard ChibiOS streams.
    */
    void* vmt; 
    uint8_t* buffer;
    size_t size;
    size_t eos; 
} MemoryStream;

#ifdef __cplusplus
extern "C" {
#endif
  void msObjectInit(MemoryStream *msp, uint8_t *buffer, size_t size, size_t eos);
#ifdef __cplusplus
}
#endif
