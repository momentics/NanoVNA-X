"""Timing tests by @jebcampbellhm (PR #11), adapted to suspend/resume capture.

The complete sweep/runtime translation units depend on STM32 assembly and UI
globals. Extract the named functions verbatim rather than duplicating their
logic; compilation fails if their interfaces change without updating the mocks.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


sweep = (ROOT / 'src/rf/sweep.c').read_text()
driver = (ROOT / 'src/driver/si5351.c').read_text()
runtime = (ROOT / 'src/runtime/runtime_entry.c').read_text()
runtime = re.sub(r'/\*.*?\*/|//[^\n]*', '', runtime, flags=re.S)
# Peripheral initialization is performed once; generator/codec clocks precede slave I2S.
order = [runtime.index(token) for token in (
    'platform_init();', 'drivers->generator->init();',
    'tlv320aic3204_init();', 'tlv320aic3204_start_clocks();', 'init_i2s(')]
assert order == sorted(order), 'generator and codec clocks must precede slave I2S'
assert 'drivers->init();' not in runtime, 'platform_init owns peripheral initialization'

prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <setjmp.h>
typedef uint32_t systime_t;
typedef int16_t audio_sample_t;
#define AUDIO_BUFFER_LEN 192
#define STM32_DMA_ISR_TCIF 1
#define MS2ST(x) ((uint32_t)(x) * 100)
#define DELAY_SWEEP_START 10
#define SWEEP_CH0_MEASURE 1
#define SWEEP_CH1_MEASURE 2
#define SWEEP_USE_INTERPOLATION 4
#define RF_STATE_SETUP_MEASURE 1
static uint32_t now, elapsed, reads, resets, processed;
static systime_t capture_start, capture_delay;
static volatile uint16_t wait_count;
static unsigned capture_sleeps, complete_after;
static audio_sample_t rx_buffer[AUDIO_BUFFER_LEN * 2];
static struct { unsigned _bandwidth; } config;
static unsigned locked, p_sweep;
static void* capture_thread;
#define MSG_OK 0
#define chSysLock osalSysLock
#define chSysUnlock osalSysUnlock
#define chSysLockFromISR osalSysLock
#define chSysUnlockFromISR osalSysUnlock
static void chThdResumeI(void** ref, int msg) { (void)ref; (void)msg; }
static volatile uint32_t fake_adc;
static bool adc_changes;
static jmp_buf halted;
static systime_t chVTGetSystemTimeX(void) { return now; }
static void chThdSleep(systime_t ticks) {
  assert(!locked && ticks > 0);
  now += ticks;
  // A lower-priority task can run only when the capture waiter blocks.
  capture_sleeps++;
  if (complete_after && capture_sleeps == complete_after) wait_count = 0;
}
static void chThdSleepMilliseconds(uint32_t ms) {
  now += MS2ST(ms); elapsed += MS2ST(ms);
  if (adc_changes && elapsed >= MS2ST(3)) fake_adc = 2;
}
static void osalSysLock(void) { assert(!locked); locked = 1; }
static void osalSysUnlock(void) { assert(locked); locked = 0; }
static int chThdSuspendTimeoutS(void** ref, systime_t timeout) {
  (void)ref;
  assert(locked);
  locked = 0;
  if (complete_after) {
    while (capture_sleeps < complete_after) chThdSleep(1);
    locked = 1;
    return MSG_OK;
  }
  now += timeout;
  locked = 1;
  return -1;
}
static void osalSysHalt(const char *reason) { (void)reason; longjmp(halted, 1); }
static bool si5351_bulk_read(uint8_t reg, uint8_t *p, int len) {
  (void)reg; (void)len; reads++; *p = 0x60; return true;
}
static void reset_dsp_accumerator(void) { resets++; }
static void dsp_process(audio_sample_t *p, unsigned n) { (void)p; (void)n; processed++; }
typedef struct {
  uint32_t frequency;
  unsigned mask;
  int delay, interpolation_idx, total_cycles, current_cycle, st_delay, state;
} rf_fsm_context_t;
static uint32_t get_frequency(unsigned i) { return 1000000 + i * 1000; }
static int app_measurement_set_frequency(uint32_t f) { (void)f; return 20; }
static uint8_t si5351_take_settling_cycles(void) { return 1; }
#include "driver/adc_startup.h"
'''
source = prefix + '\n'.join([
    function(sweep, 'void i2s_lld_serve_rx_interrupt('),
    function(sweep, 'void sweep_service_start_capture('),
    function(sweep, 'bool sweep_service_wait_for_capture('),
    function(sweep, 'static void fsm_setup_freq('),
    function(driver, 'static void si5351_wait_pll_lock('),
]) + r'''
static void check_capture(uint32_t start, uint32_t delay) {
  now = start; resets = processed = 0;
  sweep_service_start_capture(delay);
  assert(!locked);
  now = start + delay - 1;
  i2s_lld_serve_rx_interrupt(0);
  assert(wait_count == 2 && resets == 0 && processed == 0);
  now = start + delay;
  i2s_lld_serve_rx_interrupt(0);
  assert(wait_count == 1 && resets == 1 && processed == 0);
  now += 50;
  i2s_lld_serve_rx_interrupt(1);
  assert(wait_count == 0 && processed == 1);
}
int main(void) {
  wait_count = 0; capture_sleeps = 0;
  assert(sweep_service_wait_for_capture() && capture_sleeps == 0);
  wait_count = 2; complete_after = 3; now = UINT32_MAX - 1;
  assert(sweep_service_wait_for_capture() && capture_sleeps == 3 && now == 1);
  wait_count = 2; complete_after = 0; capture_sleeps = 0; now = UINT32_MAX - 1;
  systime_t start = now;
  assert(!sweep_service_wait_for_capture());
  assert(wait_count == 0 && (uint32_t)(now-start) == MS2ST(2000));
  check_capture(1000, 200);
  check_capture(UINT32_MAX - 100, 200); // Deadline crosses rollover.
  now = UINT32_MAX - 100; sweep_service_start_capture(50);
  now = 10; i2s_lld_serve_rx_interrupt(0); // Deadline passed before rollover.
  assert(wait_count == 1);
  now = UINT32_MAX; sweep_service_start_capture(0);
  i2s_lld_serve_rx_interrupt(0); assert(wait_count == 1);

  now = 1000; elapsed = reads = 0; si5351_wait_pll_lock();
  assert(reads == 100 && elapsed == MS2ST(105));
  now = UINT32_MAX - 5000; elapsed = reads = 0; si5351_wait_pll_lock();
  assert(reads == 100 && elapsed == MS2ST(105));

  rf_fsm_context_t ctx = {.mask = SWEEP_CH0_MEASURE | SWEEP_CH1_MEASURE};
  p_sweep = 0; fsm_setup_freq(&ctx); assert(ctx.st_delay == DELAY_SWEEP_START);
  assert(ctx.total_cycles == 2);
  p_sweep = 1; fsm_setup_freq(&ctx); assert(ctx.st_delay == 0);
  p_sweep = 100; fsm_setup_freq(&ctx); assert(ctx.st_delay == 0);
  p_sweep = 0; fsm_setup_freq(&ctx); assert(ctx.st_delay == DELAY_SWEEP_START);

  now = UINT32_MAX - 100; elapsed = 0; fake_adc = 1; adc_changes = true;
  adc_wait_startup(&fake_adc, 1, 0); assert(elapsed == MS2ST(3));
  adc_wait_startup(&fake_adc, 2, 2); assert(elapsed == MS2ST(3));
  now = UINT32_MAX - 100; elapsed = 0; fake_adc = 1; adc_changes = false;
  if (setjmp(halted) == 0) {
    adc_wait_startup(&fake_adc, 1, 0);
    assert(!"stuck ADC must halt instead of continuing startup");
  }
  assert(elapsed == MS2ST(10));
  puts("[PASS] startup ordering, RTOS capture blocking/timeout, capture/PLL rollover, sweep-start delay, ADC readiness/timeout");
}
'''
with tempfile.TemporaryDirectory(prefix='nanovna-timing-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(source)
    subprocess.run(shlex.split(os.environ.get('HOST_CC', 'gcc')) + [
        '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'include'),
        str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
