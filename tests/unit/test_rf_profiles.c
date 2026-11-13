/* Register-level coverage of the production RF driver, without hardware. */
#define RF_DRIVER_HOST_TEST
#include "../../src/driver/si5351.c"
#include <assert.h>
#include <stdio.h>

config_t config;
static uint8_t registers[256];
static unsigned writes;
static uint8_t left_gain, right_gain;

void i2c_transfer(uint8_t address, const uint8_t* data, size_t size) {
  assert(address == 0x60 && size > 1);
  for (size_t i = 1; i < size; ++i)
    registers[(uint8_t)(data[0] + i - 1)] = data[i];
  ++writes;
}

bool i2c_receive(uint8_t address, const uint8_t* tx, size_t tx_size, uint8_t* rx, size_t rx_size) {
  assert(address == 0x60 && tx_size == 1);
  memset(rx, 0, rx_size); /* PLL status is locked. */
  (void)tx;
  return true;
}

void tlv320aic3204_set_gain(uint8_t left, uint8_t right) {
  left_gain = left;
  right_gain = right;
}
void chThdSleepMicroseconds(uint32_t delay) { (void)delay; }
void chThdSleepMilliseconds(uint32_t delay) { (void)delay; }
systime_t chVTGetSystemTimeX(void) { return 0; }

int main(void) {
  config._xtal_freq = XTALFREQ;
  config._harmonic_freq_threshold = FREQUENCY_THRESHOLD;
  for (unsigned mode = 0; mode < SI5351_BAND_COUNT + 2; ++mode) {
    si5351_set_band_mode(mode);
#ifdef __ZEETK__
    assert(config._band_mode == SI5351_BAND_SJWCH5351);
#else
    assert(config._band_mode == (mode < SI5351_BAND_COUNT ? mode : 0));
#endif
    assert(current_freq == 0 && current_band == 0);
    const uint32_t thresholds[] = {0, FREQUENCY_THRESHOLD, FREQUENCY_MAX, UINT32_MAX};
    for (unsigned t = 0; t < ARRAY_COUNT(thresholds); ++t) {
      config._harmonic_freq_threshold = thresholds[t];
      assert(si5351_get_harmonic_lvl(UINT32_MAX) < band_count);
    }
    config._harmonic_freq_threshold = FREQUENCY_THRESHOLD;
    const uint32_t frequencies[] = {600, 32000, 1000000, 130000000, 300000000,
                                   588000000, 900000000, 2700000000U, UINT32_MAX};
    for (unsigned i = 0; i < ARRAY_COUNT(frequencies); ++i) {
      uint32_t frequency = frequencies[i];
      uint8_t band = si5351_get_harmonic_lvl(frequency);
      assert(band > 0 && band < band_count);
      if (frequency > 2700000000U)
        continue;
      si5351_set_frequency(frequency, SI5351_CLK_DRIVE_STRENGTH_AUTO);
      assert(si5351_get_frequency() == frequency);
      unsigned previous = writes;
      si5351_set_frequency(frequency, SI5351_CLK_DRIVE_STRENGTH_AUTO);
      assert(writes == previous);
      si5351_set_band_mode(mode);
      si5351_set_frequency(frequency, SI5351_CLK_DRIVE_STRENGTH_AUTO);
      assert(writes > previous);
    }
  }
  si5351_set_band_mode(SI5351_BAND_SJWCH5351);
  si5351_set_frequency(400000000, SI5351_CLK_DRIVE_STRENGTH_AUTO);
  assert((registers[16] & 3) ==
#ifdef __ZEETK__
         SI5351_CLK_DRIVE_STRENGTH_6MA
#else
         SI5351_CLK_DRIVE_STRENGTH_4MA
#endif
  );
#ifdef __ZEETK__
  assert((registers[17] & 3) == SI5351_CLK_DRIVE_STRENGTH_6MA);
  assert(left_gain == 5 && right_gain == 5);
  si5351_set_frequency(2700000000U, SI5351_CLK_DRIVE_STRENGTH_AUTO);
  assert(left_gain == 50 && right_gain == 50);
  assert((registers[16] & 3) == SI5351_CLK_DRIVE_STRENGTH_8MA);
#endif
  si5351_set_frequency(400000000, SI5351_CLK_DRIVE_STRENGTH_2MA);
  assert((registers[16] & 3) == 0 && (registers[17] & 3) == 0);
  puts("[PASS] RF profile registers, cache, bounds and runtime selection");
  return 0;
}
