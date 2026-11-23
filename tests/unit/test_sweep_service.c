/* Host regressions linked against the production sweep service and VNA math. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rf/sweep.h"
#include "hal.h"
#include "driver/si5351.h"

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); \
  } \
} while (0)

properties_t current_props;
config_t config;
float measured[2][SWEEP_POINTS_MAX][2];

static unsigned test_count;
static systime_t now;
static unsigned time_reads;
static unsigned thread_locks;
static unsigned isr_locks;
static unsigned suspend_calls;
static unsigned resume_calls;
static unsigned reset_calls;
static unsigned process_calls;
static unsigned callback_calls;
static unsigned timeout_budget;
static int lock_class;
static bool in_isr;
static bool complete_on_lock;
static bool resumed;
static thread_t capture_worker;
static thread_reference_t* suspended_reference;
static bool tuning_fails;
static int failing_channel;
static unsigned tune_calls, select_calls, cache_resets, receiver_starts;
static uint8_t selected_channel;

static void capture_irq(void) {
  CHECK(lock_class == 0);
  CHECK(!in_isr);
  in_isr = true;
  uint32_t flags = (callback_calls++ & 1U) ? STM32_DMA_ISR_TCIF : 0;
  i2s_lld_serve_rx_interrupt(flags);
  CHECK(lock_class == 0);
  in_isr = false;
}

static void complete_capture(void) {
  /* One priming DMA block, then bandwidth + 1 accumulated blocks. */
  unsigned count = config._bandwidth + 2U;
  for (unsigned i = 0; i < count; ++i)
    capture_irq();
  CHECK(process_calls == config._bandwidth + 1U);
}

void chSysLock(void) {
  CHECK(!in_isr);
  CHECK(lock_class == 0);
  if (complete_on_lock) {
    complete_on_lock = false;
    complete_capture();
  }
  lock_class = 1;
  ++thread_locks;
}

void chSysUnlock(void) {
  CHECK(!in_isr);
  CHECK(lock_class == 1);
  lock_class = 0;
}

void osalSysLock(void) {
  chSysLock();
}

void osalSysUnlock(void) {
  chSysUnlock();
}

void chSysLockFromISR(void) {
  CHECK(in_isr);
  CHECK(lock_class == 0);
  lock_class = 2;
  ++isr_locks;
}

void chSysUnlockFromISR(void) {
  CHECK(in_isr);
  CHECK(lock_class == 2);
  lock_class = 0;
}

msg_t chThdSuspendTimeoutS(thread_reference_t* reference, systime_t timeout) {
  CHECK(!in_isr);
  CHECK(lock_class == 1);
  CHECK(reference != NULL && *reference == NULL);
  CHECK(suspended_reference == NULL);
  CHECK(process_calls < config._bandwidth + 1U);
  CHECK(timeout == MS2ST(2000));
  CHECK(++suspend_calls < 8); /* Bound retries so polling regressions fail fast. */

  /* ChibiOS installs the reference before atomically unlocking/blocking. */
  *reference = &capture_worker;
  suspended_reference = reference;
  resumed = false;
  lock_class = 0;
  msg_t result;
  if (timeout_budget) {
    --timeout_budget;
    now += timeout;
    *reference = NULL;
    result = MSG_TIMEOUT;
  } else {
    /* Pending ISR runs at the earliest legal handoff, never in the check gap. */
    now += 2; /* The simulated DMA block arrives after the sweep settling delays. */
    complete_capture();
    CHECK(resumed);
    CHECK(*reference == NULL);
    result = MSG_OK;
  }
  suspended_reference = NULL;
  CHECK(lock_class == 0);
  lock_class = 1; /* The S-class call returns with the system lock held. */
  return result;
}

void chThdResumeI(thread_reference_t* reference, msg_t message) {
  CHECK(in_isr);
  CHECK(lock_class == 2);
  CHECK(reference != NULL);
  CHECK(message == MSG_OK);
  CHECK(process_calls == config._bandwidth + 1U);
  ++resume_calls;
  if (*reference != NULL) {
    CHECK(reference == suspended_reference);
    CHECK(*reference == &capture_worker);
    CHECK(!resumed);
    *reference = NULL;
    resumed = true;
  }
}

systime_t chVTGetSystemTimeX(void) {
  CHECK(++time_reads < 100); /* No polling clock or WFI busy-wait fallback. */
  return now;
}

void sweep_test_wfi(void) {
  CHECK(!"capture must suspend instead of executing WFI");
}

void reset_dsp_accumerator(void) {
  CHECK(in_isr && lock_class == 0);
  CHECK(reset_calls == 0);
  ++reset_calls;
}

void dsp_process(audio_sample_t* samples, size_t count) {
  CHECK(in_isr && lock_class == 0);
  CHECK(reset_calls == 1);
  CHECK(count == AUDIO_BUFFER_LEN);
  size_t offset = ((callback_calls - 1U) & 1U) ? AUDIO_BUFFER_LEN : 0;
  CHECK(samples == sweep_service_rx_buffer() + offset);
  ++process_calls;
}

void calculate_gamma(float* gamma) {
  (void)gamma;
  CHECK(!"capture-only tests must not sample gamma");
}

void request_to_redraw(uint16_t flags) {
  CHECK(flags == REDRAW_CAL_STATUS);
}

systime_t chVTTimeElapsedSinceX(systime_t start) { return now - start; }

void palClearPad(int port, int pad) { (void)port; (void)pad; }
void palSetPad(int port, int pad) { (void)port; (void)pad; }

bool event_bus_publish(event_bus_t* bus, event_bus_topic_t topic, const void* payload) {
  (void)bus;
  (void)topic;
  (void)payload;
  CHECK(!"this test does not install an event bus");
  return false;
}

int si5351_set_frequency(uint32_t freq, uint8_t drive_strength) {
  (void)drive_strength;
  CHECK(freq == 1000000U || freq == 2000000U);
  ++tune_calls;
  return tuning_fails ? -1 : 0;
}

uint32_t si5351_get_harmonic_lvl(uint32_t freq) { (void)freq; return 1; }
uint8_t si5351_take_settling_cycles(void) { return 0; }
void si5351_reset_cache(void) { ++cache_resets; }

void init_i2s(void* buffer, uint16_t count) {
  CHECK(buffer == sweep_service_rx_buffer());
  CHECK(count == 2U * AUDIO_BUFFER_LEN * sizeof(audio_sample_t) / sizeof(int16_t));
  ++receiver_starts;
}

bool tlv320aic3204_select(uint8_t channel) {
  ++select_calls;
  selected_channel = channel;
  reset_calls = process_calls = callback_calls = 0;
  return channel != failing_channel;
}

static void sample_sweep_point(float* data) {
  CHECK(process_calls == 1);
  data[0] = selected_channel + 1.0f;
  data[1] = -data[0];
}

static void reset_capture(uint16_t bandwidth) {
  CHECK(lock_class == 0 && !in_isr && suspended_reference == NULL);
  sweep_service_init(NULL);
  now = 100;
  time_reads = thread_locks = isr_locks = suspend_calls = resume_calls = 0;
  reset_calls = process_calls = callback_calls = 0;
  timeout_budget = 0;
  complete_on_lock = resumed = false;
  config._bandwidth = bandwidth;
}

static void test_tuning_and_input_errors(void) {
  reset_capture(0);
  sweep_points = 2;
  app_measurement_set_frequencies(1000000U, 2000000U, sweep_points);
  sweep_service_set_sample_function(sample_sweep_point);
  memset(measured, 0, sizeof measured);
  const uint16_t mask = SWEEP_CH0_MEASURE | SWEEP_CH1_MEASURE;
  tune_calls = select_calls = cache_resets = 0;
  failing_channel = -1;
  tuning_fails = true;
  CHECK(!app_measurement_sweep(false, mask));
  CHECK(tune_calls == 1 && select_calls == 0 && suspend_calls == 0);
  CHECK(receiver_starts == 0);
  CHECK(!sweep_service_data_valid());

  tuning_fails = false;
  failing_channel = 1;
  CHECK(!app_measurement_sweep(false, mask));
  CHECK(tune_calls == 2 && select_calls == 2 && suspend_calls == 1);
  CHECK(cache_resets == 1 && !sweep_service_data_valid());
  CHECK(measured[0][0][0] == 0.0f && measured[1][0][0] == 0.0f);

  failing_channel = -1;
  CHECK(app_measurement_sweep(false, mask));
  CHECK(tune_calls == 4 && select_calls == 6 && suspend_calls == 5);
  CHECK(receiver_starts == 1);
  for (unsigned point = 0; point < sweep_points; ++point) {
    CHECK(measured[0][point][0] == 1.0f && measured[0][point][1] == -1.0f);
    CHECK(measured[1][point][0] == 2.0f && measured[1][point][1] == -2.0f);
  }
  ++test_count;
}

static void test_capture_idle(void) {
  reset_capture(0);
  CHECK(sweep_service_wait_for_capture());
  capture_irq();
  CHECK(thread_locks == 1 && lock_class == 0);
  CHECK(suspend_calls == 0 && resume_calls == 0);
  CHECK(reset_calls == 0 && process_calls == 0 && isr_locks == 0);
  ++test_count;
}

static void test_capture_before_wait(void) {
  reset_capture(2);
  sweep_service_start_capture(10);
  now = 109;
  capture_irq();
  CHECK(reset_calls == 0 && process_calls == 0);
  now = 110;
  capture_irq();
  CHECK(reset_calls == 1 && process_calls == 0);
  capture_irq();
  CHECK(process_calls == 1);
  capture_irq();
  CHECK(process_calls == 2);
  capture_irq();
  CHECK(process_calls == 3);
  CHECK(isr_locks == 1 && resume_calls == 1);
  CHECK(sweep_service_wait_for_capture());
  CHECK(suspend_calls == 0 && lock_class == 0);
  capture_irq();
  CHECK(process_calls == 3 && resume_calls == 1);
  ++test_count;
}

static void test_capture_check_boundary(void) {
  reset_capture(1);
  sweep_service_start_capture(0);
  /* Complete immediately before the waiter's locked readiness check. */
  complete_on_lock = true;
  CHECK(sweep_service_wait_for_capture());
  CHECK(!complete_on_lock);
  CHECK(suspend_calls == 0 && resume_calls == 1);
  CHECK(process_calls == 2 && lock_class == 0);
  ++test_count;
}

static void test_capture_suspend_handoff(void) {
  reset_capture(3);
  sweep_service_start_capture(0);
  CHECK(sweep_service_wait_for_capture());
  CHECK(suspend_calls == 1 && resume_calls == 1 && isr_locks == 1);
  CHECK(reset_calls == 1 && process_calls == 4);
  CHECK(lock_class == 0);
  CHECK(sweep_service_wait_for_capture());
  CHECK(suspend_calls == 1);
  ++test_count;
}

static void test_capture_timeout_retry(void) {
  reset_capture(0);
  sweep_service_start_capture(0);
  timeout_budget = 1;
  CHECK(!sweep_service_wait_for_capture());
  CHECK(suspend_calls == 1 && resume_calls == 0);
  CHECK(lock_class == 0);
  CHECK(reset_calls == 0 && process_calls == 0);
  /* Timeout cancels the acquisition; a new capture installs a fresh waiter. */
  CHECK(sweep_service_wait_for_capture());
  CHECK(suspend_calls == 1);
  sweep_service_start_capture(0);
  CHECK(sweep_service_wait_for_capture());
  CHECK(suspend_calls == 2 && resume_calls == 1);
  CHECK(process_calls == 1);
  CHECK(lock_class == 0);
  ++test_count;
}

static void test_capture_after_timeout(void) {
  reset_capture(0);
  sweep_service_start_capture(0);
  timeout_budget = 1;
  CHECK(!sweep_service_wait_for_capture());
  /* A late interrupt must not resume a stale timed-out thread reference. */
  capture_irq();
  capture_irq();
  CHECK(!resumed && resume_calls == 0 && process_calls == 0);
  CHECK(sweep_service_wait_for_capture());
  CHECK(suspend_calls == 1 && lock_class == 0);
  /* A subsequent acquisition must not inherit any waiter or sample count. */
  reset_calls = process_calls = callback_calls = 0;
  sweep_service_start_capture(0);
  CHECK(sweep_service_wait_for_capture());
  CHECK(suspend_calls == 2 && resume_calls == 1 && resumed);
  CHECK(process_calls == 1 && lock_class == 0);
  ++test_count;
}

static void test_dump_capture(void) {
  static _Alignas(4) audio_sample_t dump[AUDIO_BUFFER_LEN * 2 + 2];
  audio_sample_t* rx = (audio_sample_t*)sweep_service_rx_buffer();
  for (unsigned selection = 0; selection < 2; ++selection) {
    for (unsigned bandwidth = 0; bandwidth < 4; ++bandwidth) {
      reset_capture(bandwidth);
      for (unsigned i = 0; i < AUDIO_BUFFER_LEN * 2; ++i)
        rx[i] = (audio_sample_t)(1000 + i);
      for (unsigned i = 0; i < ARRAY_COUNT(dump); ++i)
        dump[i] = -123;
      sweep_service_prepare_dump(dump, AUDIO_BUFFER_LEN * 2, selection);
      unsigned captures = 0;
      do {
        reset_calls = process_calls = callback_calls = 0;
        sweep_service_start_capture(0);
        CHECK(sweep_service_wait_for_capture());
        CHECK(++captures <= 2);
      } while (!sweep_service_dump_ready());
      for (unsigned i = 0; i < AUDIO_BUFFER_LEN * 2; ++i) {
        unsigned half = AUDIO_BUFFER_LEN / 2;
        unsigned block = (i / half) % (bandwidth + 2U);
        unsigned offset = (block & 1U) * AUDIO_BUFFER_LEN + 2U * (i % half);
        CHECK(dump[i] == rx[offset + selection]);
      }
      CHECK(dump[AUDIO_BUFFER_LEN * 2] == -123);
      CHECK(dump[AUDIO_BUFFER_LEN * 2 + 1] == -123);
      CHECK(captures == (bandwidth < 2 ? 2U : 1U));
      ++test_count;
    }
  }

  reset_capture(0);
  sweep_service_prepare_dump(dump, AUDIO_BUFFER_LEN * 2, 0);
  sweep_service_start_capture(0);
  timeout_budget = 1;
  CHECK(!sweep_service_wait_for_capture());
  sweep_service_prepare_dump(NULL, 0, 0);
  CHECK(sweep_service_dump_ready());
  sweep_service_start_capture(0);
  CHECK(sweep_service_wait_for_capture());
  CHECK(sweep_service_dump_ready());
  ++test_count;
}

static void reference_smooth(float data[2][SWEEP_POINTS_MAX][2], uint16_t points,
                             unsigned factor, unsigned mask, bool arithmetic) {
  float previous[2][SWEEP_POINTS_MAX][2];
  unsigned passes = factor == 0 || points < 3 ? 0 : 1U << (factor - 1U);
  if (points >= 3 && passes > points - 2U)
    passes = points - 2U;
  for (unsigned pass = 0; pass < passes; ++pass) {
    memcpy(previous, data, sizeof previous);
    for (unsigned ch = 0; ch < 2; ++ch) {
      if (!(mask & (1U << ch)))
        continue;
      for (unsigned i = 1; i + 1 < points; ++i) {
        for (unsigned component = 0; component < 2; ++component) {
          float a = previous[ch][i - 1][component];
          float b = previous[ch][i][component];
          float c = previous[ch][i + 1][component];
          float value = (a + 2.0f * b + c) / 4.0f;
          if (!arithmetic) {
            value = cbrtf(fabsf(a * b * c));
            if (a + b + c < 0)
              value = -value;
          }
          data[ch][i][component] = value;
        }
      }
    }
  }
}

static void test_smoothing(void) {
  static const uint16_t lengths[] = {0, 1, 2, 3, 7, SWEEP_POINTS_MAX};
  static const unsigned masks[] = {0, SWEEP_CH0_MEASURE, SWEEP_CH1_MEASURE,
                                  SWEEP_CH0_MEASURE | SWEEP_CH1_MEASURE,
                                  SWEEP_APPLY_CALIBRATION};
  static const float values[] = {1, 8, 27, -64, -125, 0, 216, -8, 1};
  float original[2][SWEEP_POINTS_MAX][2];
  float expected[2][SWEEP_POINTS_MAX][2];
  for (unsigned ch = 0; ch < 2; ++ch)
    for (unsigned i = 0; i < SWEEP_POINTS_MAX; ++i)
      for (unsigned k = 0; k < 2; ++k)
        original[ch][i][k] = values[(i + 2 * ch + 4 * k) % 9];

  for (unsigned arithmetic = 0; arithmetic < 2; ++arithmetic) {
    config._vna_mode = arithmetic ? (1U << VNA_MODE_SMOOTH) : 0;
    for (unsigned length = 0; length < sizeof lengths / sizeof lengths[0]; ++length) {
      current_props._sweep_points = lengths[length];
      for (unsigned factor = 0; factor <= 9; ++factor) {
        unsigned requested = factor == 9 ? 255 : factor;
        unsigned effective = requested > 8 ? 8 : requested;
        set_smooth_factor((uint8_t)requested);
        CHECK(get_smooth_factor() == effective);
        for (unsigned mask_idx = 0; mask_idx < sizeof masks / sizeof masks[0]; ++mask_idx) {
          unsigned mask = masks[mask_idx];
          memcpy(measured, original, sizeof measured);
          memcpy(expected, original, sizeof expected);
          reference_smooth(expected, lengths[length], effective, mask, arithmetic);
          measurement_data_smooth((uint16_t)mask);
          for (unsigned ch = 0; ch < 2; ++ch) {
            for (unsigned i = 0; i < SWEEP_POINTS_MAX; ++i) {
              bool untouched = effective == 0 || !(mask & (1U << ch)) || i == 0 ||
                               i + 1 >= lengths[length];
              for (unsigned k = 0; k < 2; ++k) {
                CHECK(isfinite(measured[ch][i][k]));
                if (untouched) {
                  CHECK(measured[ch][i][k] == original[ch][i][k]);
                } else {
                  float tolerance = 0.0003f * fmaxf(1.0f, fabsf(expected[ch][i][k]));
                  CHECK(fabsf(measured[ch][i][k] - expected[ch][i][k]) <= tolerance);
                }
              }
            }
          }
          ++test_count;
        }
      }
    }
  }
}

int main(void) {
  sweep_service_init(NULL);
  CHECK(!sweep_service_data_valid());
  sweep_service_increment_generation();
  CHECK(sweep_service_data_valid());
  sweep_service_begin_measurement();
  CHECK(!sweep_service_data_valid());
  sweep_service_end_measurement();
  CHECK(!sweep_service_data_valid());
  sweep_service_increment_generation();
  app_measurement_reset();
  CHECK(!sweep_service_data_valid());
  test_dump_capture();
  test_smoothing();
  test_capture_idle();
  test_capture_before_wait();
  test_capture_check_boundary();
  test_capture_suspend_handoff();
  test_capture_timeout_retry();
  test_capture_after_timeout();
  test_tuning_and_input_errors();
  printf("sweep service: %u regression cases passed\n", test_count);
  return EXIT_SUCCESS;
}
