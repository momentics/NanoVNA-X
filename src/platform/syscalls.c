/*
 * Minimal system call implementations required by newlib.
 *
 * Provides a simple heap backed by the linker-defined __heap_base__ and
 * __heap_end__ symbols so that the C library can satisfy dynamic memory
 * requests. The heap grows linearly and no deallocation support is
 * provided beyond the optional ability to shrink through a negative
 * increment, matching the behaviour expected by _sbrk().
 * 
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


#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/reent.h>
#include <sys/types.h>

static uint8_t *heap_current = NULL;

extern uint8_t __heap_base__;
extern uint8_t __heap_end__;

void * _sbrk(ptrdiff_t incr) {
  uint8_t *heap_base = &__heap_base__;
  uint8_t *heap_limit = &__heap_end__;

  if (heap_current == NULL) {
    heap_current = heap_base;
  }

  if (incr == 0) {
    return heap_current;
  }

  if (incr > 0) {
    ptrdiff_t remaining = (ptrdiff_t)(heap_limit - heap_current);

    if (remaining < incr) {
      errno = ENOMEM;
      return (void *)-1;
    }
  } else {
    ptrdiff_t used = (ptrdiff_t)(heap_current - heap_base);

    if (used < -incr) {
      errno = ENOMEM;
      return (void *)-1;
    }
  }

  uint8_t *previous = heap_current;
  heap_current = previous + incr;

  return previous;
}

caddr_t _sbrk_r(struct _reent *r, ptrdiff_t incr) {
  void *result = _sbrk(incr);

  if (result == (void *)-1) {
    __errno_r(r) = errno;
  }

  return (caddr_t)result;
}

