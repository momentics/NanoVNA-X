#ifndef NANOVNA_ADC_STARTUP_H
#define NANOVNA_ADC_STARTUP_H

// Startup recovery contributed by @jebcampbellhm in PR #11:
// https://github.com/momentics/NanoVNA-X/pull/11
// Startup only: never continue with an uncalibrated or unready ADC.
// The runtime watchdog is running before these waits and will recover a halt.
static inline void adc_wait_startup(volatile uint32_t* reg, uint32_t mask,
                                    uint32_t expected) {
  systime_t start = chVTGetSystemTimeX();
  while ((*reg & mask) != expected) {
    if ((systime_t)(chVTGetSystemTimeX() - start) >= MS2ST(10)) {
      osalSysHalt("ADC startup timeout");
    }
    chThdSleepMilliseconds(1);
  }
}

#endif
