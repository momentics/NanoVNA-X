/*
 * Copyright (c) 2019-2020, Dmitry (DiSlord) dislordlive@gmail.com
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

#define VNA_I2C                         I2C1
#define I2C_CR2_SADD_7BIT_SHIFT         1
#define I2C_CR2_NBYTES_SHIFT            16

void i2c_set_timings(uint32_t timings) {
  VNA_I2C->CR1&=~I2C_CR1_PE;
  VNA_I2C->TIMINGR = timings;
  VNA_I2C->CR1|= I2C_CR1_PE;
}

void i2c_start(void) {
  rccEnableI2C1(FALSE);
  i2c_set_timings(STM32_I2C_INIT_T);
}

// Bounded I2C recovery contributed by @jebcampbellhm in PR #13.
// Bound every peripheral wait, including BUSY and a missing ACK/clock.
// The iteration cap also terminates during early startup with the tick stopped.
static bool i2c_wait(uint32_t mask, uint32_t value) {
  for (unsigned timeout=100000;timeout;--timeout) {
    uint32_t status=VNA_I2C->ISR;
    if (status & (I2C_ISR_NACKF|I2C_ISR_BERR|I2C_ISR_ARLO)) break;
    if ((status & mask)==value) return true;
  }
  VNA_I2C->CR1 &= ~I2C_CR1_PE;
  VNA_I2C->ICR = I2C_ICR_NACKCF|I2C_ICR_STOPCF|I2C_ICR_BERRCF|I2C_ICR_ARLOCF;
  return false;
}

bool i2c_receive(uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn) {
  if (wn>255 || rn>255 || (!wn && !rn) || (wn && !w) || (rn && !r)) return false;
  if (!i2c_wait(I2C_ISR_BUSY,0)) return false;
  VNA_I2C->CR1 |= I2C_CR1_PE;
  if (wn) {
    VNA_I2C->CR2=(addr<<1)|(wn<<16)|I2C_CR2_START|(rn ? 0 : I2C_CR2_AUTOEND);
    do {
      if (!i2c_wait(I2C_ISR_TXIS,I2C_ISR_TXIS)) return false;
      VNA_I2C->TXDR=*w++;
    } while (--wn);
    if (rn && !i2c_wait(I2C_ISR_TC,I2C_ISR_TC)) return false;
  }
  if (rn) {
    VNA_I2C->CR2=(addr<<1)|(rn<<16)|I2C_CR2_RD_WRN|I2C_CR2_START|I2C_CR2_AUTOEND;
    do {
      if (!i2c_wait(I2C_ISR_RXNE,I2C_ISR_RXNE)) return false;
      *r++=VNA_I2C->RXDR;
    } while (--rn);
  }
  if (!i2c_wait(I2C_ISR_STOPF,I2C_ISR_STOPF)) return false;
  VNA_I2C->ICR=I2C_ICR_STOPCF;
  return true;
}

bool i2c_transfer(uint8_t addr, const uint8_t *w, size_t wn) {
  return i2c_receive(addr,w,wn,NULL,0);
}
