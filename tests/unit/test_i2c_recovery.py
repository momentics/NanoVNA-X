"""Run the H4 I2C implementation with register-level timeout/NACK simulations."""
from pathlib import Path
import os, shlex, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
source=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
static struct {volatile uint32_t CR1,CR2,TIMINGR,ISR,ICR,TXDR,RXDR;} registers;
#define I2C1 (&registers)
#define FALSE false
#define rccEnableI2C1(x) ((void)(x))
#define STM32_I2C_INIT_T 123u
#define I2C_CR1_PE 1u
#define I2C_CR2_START 1u
#define I2C_CR2_AUTOEND 2u
#define I2C_CR2_RD_WRN 4u
#define I2C_ISR_BUSY 1u
#define I2C_ISR_TXIS 2u
#define I2C_ISR_RXNE 4u
#define I2C_ISR_TC 8u
#define I2C_ISR_STOPF 16u
#define I2C_ISR_NACKF 32u
#define I2C_ISR_BERR 64u
#define I2C_ISR_ARLO 128u
#define I2C_ICR_NACKCF 32u
#define I2C_ICR_STOPCF 16u
#define I2C_ICR_BERRCF 64u
#define I2C_ICR_ARLOCF 128u
''' + (ROOT/'boards/STM32F303/i2c_v2.c').read_text() + r'''
int main(void) {
  uint8_t w[]={0x12,0x34},r;
  i2c_start(); assert(registers.TIMINGR==123 && registers.CR1==I2C_CR1_PE);
  assert(!i2c_transfer(0x60,w,0)); assert(!i2c_transfer(0x60,w,256));
  assert(!i2c_receive(0x60,NULL,1,&r,1));
  const uint32_t faults[]={I2C_ISR_BUSY,I2C_ISR_NACKF,I2C_ISR_BERR,I2C_ISR_ARLO,0,I2C_ISR_TXIS};
  for(unsigned i=0;i<sizeof faults/sizeof *faults;++i) {
    registers.ISR=faults[i];registers.CR1=I2C_CR1_PE;
    assert(!i2c_receive(0x60,w,2,&r,1)); assert(!(registers.CR1&I2C_CR1_PE));
  }
  registers.ISR=I2C_ISR_TXIS|I2C_ISR_TC|I2C_ISR_RXNE|I2C_ISR_STOPF;
  registers.RXDR=0x5a;
  assert(i2c_receive(0x60,w,2,&r,1) && r==0x5a && registers.TXDR==0x34);
  assert(i2c_transfer(0x60,w,2));
  registers.ISR=I2C_ISR_RXNE|I2C_ISR_STOPF; // Pure receive must not wait for TC.
  assert(i2c_receive(0x60,NULL,0,&r,1));
  puts("H4 I2C: stuck BUSY/TX/TC, NACK, bus errors, bounded recovery and pure read passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.c').write_text(source)
    subprocess.run(shlex.split(os.environ.get('HOST_CC','gcc'))+['-std=c11','-O2','-Wall',
        '-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=10)
