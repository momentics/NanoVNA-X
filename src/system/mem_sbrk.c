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

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/reent.h>

extern uint8_t __heap_base__;
extern uint8_t __heap_end__;

static uint8_t* current_break = &__heap_base__;

void* _sbrk(ptrdiff_t incr) {
  uint8_t* prev = current_break;
  uint8_t* next = prev + incr;
  if (next < &__heap_base__)
    next = &__heap_base__;
  if (next > &__heap_end__) {
    errno = ENOMEM;
    return (void*)-1;
  }
  current_break = next;
  return prev;
}

void* _sbrk_r(struct _reent* r, ptrdiff_t incr) {
  void* result = _sbrk(incr);
  if (result == (void*)-1 && r)
    r->_errno = ENOMEM;
  return result;
}