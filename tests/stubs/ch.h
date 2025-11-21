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
#include <stddef.h>
#include <stdint.h>

typedef uint32_t systime_t;
typedef intptr_t msg_t;

#ifndef CH_CFG_USE_WAITEXIT
#define CH_CFG_USE_WAITEXIT FALSE
#endif
#ifndef CH_CFG_USE_REGISTRY
#define CH_CFG_USE_REGISTRY FALSE
#endif

#define MSG_OK 0
#define MSG_TIMEOUT (-1)
#define TIME_IMMEDIATE 0
#define TIME_INFINITE (-1)

#define MS2ST(ms) ((systime_t)(ms))

typedef struct BaseSequentialStream {
  void* vmt;
} BaseSequentialStream;

typedef BaseSequentialStream BaseAsynchronousChannel;

typedef void (*tfunc_t)(void* arg);

#define THD_FUNCTION(name, arg) void name(void* arg)

typedef struct {
  msg_t* buffer;
  size_t length;
  size_t head;
  size_t tail;
  size_t count;
} mailbox_t;

typedef struct thread {
  tfunc_t entry;
  void* arg;
  bool terminated_flag;
} thread_t;

typedef int tprio_t;

typedef struct {
  int dummy;
} threads_queue_t;

size_t chnWriteTimeout(BaseAsynchronousChannel* chp, const uint8_t* data, size_t size,
                       systime_t timeout);
size_t chnReadTimeout(BaseAsynchronousChannel* chp, uint8_t* data, size_t size,
                      systime_t timeout);

void chMBObjectInit(mailbox_t* mbp, msg_t* buf, size_t n);
msg_t chMBPost(mailbox_t* mbp, msg_t msg, systime_t timeout);
msg_t chMBPostI(mailbox_t* mbp, msg_t msg);
msg_t chMBFetch(mailbox_t* mbp, msg_t* msgp, systime_t timeout);

void chSysLock(void);
void chSysUnlock(void);
void chSysLockFromISR(void);
void chSysUnlockFromISR(void);
void osalSysLock(void);
void osalSysUnlock(void);
void osalThreadQueueObjectInit(threads_queue_t* queue);
void osalThreadEnqueueTimeoutS(threads_queue_t* queue, systime_t timeout);
msg_t osalThreadDequeueNextI(threads_queue_t* queue, msg_t msg);
thread_t* chThdCreateStatic(void* warea, size_t size, tprio_t prio, tfunc_t entry, void* arg);
void chThdExit(msg_t msg);
void chThdTerminate(thread_t* tp);
void chThdWait(thread_t* tp);
bool chThdTerminatedX(thread_t* tp);
void chThdSleepMilliseconds(uint32_t ms);
