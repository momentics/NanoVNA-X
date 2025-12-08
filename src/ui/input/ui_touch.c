/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
 * All rights reserved.
 */
#include "nanovna.h"
#include "ui/input/ui_touch.h"
#include "hal.h"
#include <string.h>

// Touch screen status constants
// Touch screen status constants are in ui_touch.h
#define TOUCH_INTERRUPT_ENABLED 1

// Helper for CALIBRATION_OFFSET
#define CALIBRATION_OFFSET 20

void touch_position(int* x, int* y) {
  int16_t last_touch_x, last_touch_y;
  touch_get_last_position(&last_touch_x, &last_touch_y);

#ifdef __REMOTE_DESKTOP__
  if (touch_is_remote()) {
    *x = last_touch_x;
    *y = last_touch_y;
    return;
  }
#endif

  static int16_t cal_cache[4] = {0};
  static int32_t scale_x = 0;
  static int32_t scale_y = 0;
  static bool scale_ready = false;

  if (!scale_ready) {
    scale_x = 1 << 16;
    scale_y = 1 << 16;
    scale_ready = true;
  }

  // Check if calibration data has changed and recalculate scales if needed
  if (memcmp(cal_cache, config._touch_cal, sizeof(cal_cache)) != 0) {
    memcpy(cal_cache, config._touch_cal, sizeof(cal_cache));

    int32_t denom_x = config._touch_cal[2] - config._touch_cal[0];
    int32_t denom_y = config._touch_cal[3] - config._touch_cal[1];

    if (denom_x != 0 && denom_y != 0) {
      scale_x = ((int32_t)(LCD_WIDTH - 1 - 2 * CALIBRATION_OFFSET) << 16) / denom_x;
      scale_y = ((int32_t)(LCD_HEIGHT - 1 - 2 * CALIBRATION_OFFSET) << 16) / denom_y;
    } else {
      scale_x = 1 << 16;
      scale_y = 1 << 16;
    }
  }

  int tx, ty;
  tx = (int)(((int64_t)scale_x * (last_touch_x - config._touch_cal[0])) >> 16) + CALIBRATION_OFFSET;
  if (tx < 0)
    tx = 0;
  else if (tx >= LCD_WIDTH)
    tx = LCD_WIDTH - 1;

  ty = (int)(((int64_t)scale_y * (last_touch_y - config._touch_cal[1])) >> 16) + CALIBRATION_OFFSET;
  if (ty < 0)
    ty = 0;
  else if (ty >= LCD_HEIGHT)
    ty = LCD_HEIGHT - 1;

#ifdef __FLIP_DISPLAY__
  if (VNA_MODE(VNA_MODE_FLIP_DISPLAY)) {
    tx = LCD_WIDTH - 1 - tx;
    ty = LCD_HEIGHT - 1 - ty;
  }
#endif
  *x = tx;
  *y = ty;
}

#define TOUCH_INTERRUPT_ENABLED 1

// Cooperative polling budgets
#define TOUCH_RELEASE_POLL_INTERVAL_MS 2U // 500 Hz release detection
#define TOUCH_DRAG_POLL_INTERVAL_MS 8U    // 125 Hz drag updates

static uint8_t touch_status_flag = 0;
static uint8_t last_touch_status = EVT_TOUCH_NONE;
static int16_t last_touch_x;
static int16_t last_touch_y;

#ifdef __REMOTE_DESKTOP__
static uint8_t touch_remote = REMOTE_NONE;
void remote_touch_set(uint16_t state, int16_t x, int16_t y) {
  touch_remote = state;
  if (x != -1)
    last_touch_x = x;
  if (y != -1)
    last_touch_y = y;
  // This needs access to ui_controller function, so maybe callbacks or extern?
  // ui_controller_publish_board_event(BOARD_EVENT_TOUCH, 0, false);
  // For now, we will handle this dependency later or via callback.
}
#endif

// Forward declaration of internal function
static int touch_status(void);

static int touch_measure_y(void) {
  // drive low to high on X line (coordinates from top to bottom)
  palClearPad(GPIOB, GPIOB_XN);
  
  // open Y line (At this state after touch_prepare_sense)
  palSetPadMode(GPIOA, GPIOA_YP, PAL_MODE_INPUT_ANALOG); // <- ADC_TOUCH_Y channel

  //  chThdSleepMilliseconds(3);
  return adc_single_read(ADC_TOUCH_Y);
}

static int touch_measure_x(void) {
  // drive high to low on Y line (coordinates from left to right)
  palSetPad(GPIOB, GPIOB_YN);
  palClearPad(GPIOA, GPIOA_YP);
  // Set Y line as output
  palSetPadMode(GPIOB, GPIOB_YN, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOA, GPIOA_YP, PAL_MODE_OUTPUT_PUSHPULL);
  // Set X line as input
  palSetPadMode(GPIOB, GPIOB_XN, PAL_MODE_INPUT);        // Hi-z mode
  palSetPadMode(GPIOA, GPIOA_XP, PAL_MODE_INPUT_ANALOG); // <- ADC_TOUCH_X channel
  //  chThdSleepMilliseconds(3);
  return adc_single_read(ADC_TOUCH_X);
}

// Manually measure touch event
static int touch_status(void) {
  return adc_single_read(ADC_TOUCH_Y) > TOUCH_THRESHOLD;
}

static void touch_prepare_sense(void) {
  // Set Y line as input
  palSetPadMode(GPIOB, GPIOB_YN, PAL_MODE_INPUT);          // Hi-z mode
  palSetPadMode(GPIOA, GPIOA_YP, PAL_MODE_INPUT_PULLDOWN); // Use pull
  // drive high on X line (for touch sense on Y)
  palSetPad(GPIOB, GPIOB_XN);
  palSetPad(GPIOA, GPIOA_XP);
  // force high X line
  palSetPadMode(GPIOB, GPIOB_XN, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOA, GPIOA_XP, PAL_MODE_OUTPUT_PUSHPULL);
  //  chThdSleepMilliseconds(10); // Wait 10ms for denounce touch
}

void touch_start_watchdog(void) {
  if (touch_status_flag & TOUCH_INTERRUPT_ENABLED)
    return;
  touch_status_flag ^= TOUCH_INTERRUPT_ENABLED;
  adc_start_analog_watchdog();
#ifdef __REMOTE_DESKTOP__
  touch_remote = REMOTE_NONE;
#endif
}

void touch_stop_watchdog(void) {
  if (!(touch_status_flag & TOUCH_INTERRUPT_ENABLED))
    return;
  touch_status_flag ^= TOUCH_INTERRUPT_ENABLED;
  adc_stop_analog_watchdog();
}

// Touch panel timer check (check press frequency 20Hz)
#if HAL_USE_GPT == TRUE
static const GPTConfig gpt3cfg = {1000,   // 1kHz timer clock.
                                  NULL,   // Timer callback.
                                  0x0020, // CR2:MMS=02 to output TRGO
                                  0};

static void touch_init_timers(void) {
  gptStart(&GPTD3, &gpt3cfg);     // Init timer 3
  gptStartContinuous(&GPTD3, 10); // Start timer 10ms period (use 1kHz clock)
}
#else
static void touch_init_timers(void) {
  board_init_timers();
  board_start_timer(TIM3, 10); // Start timer 10ms period (use 1kHz clock)
}
#endif

//
// Touch init function init timer 3 trigger adc for check touch interrupt, and run measure
//
void touch_init(void) {
  // Prepare pin for measure touch event
  touch_prepare_sense();
  // Start touch interrupt, used timer_3 ADC check threshold:
  touch_init_timers();
  touch_start_watchdog(); // Start ADC watchdog (measure by timer 3 interval and trigger interrupt
                          // if touch pressed)
}

// Main software touch function
// return touch status
int touch_check(void) {
  touch_stop_watchdog();

  int stat = touch_status();
  if (stat) {
    int y = touch_measure_y();
    int x = touch_measure_x();
    touch_prepare_sense();
    if (touch_status()) {
      last_touch_x = x;
      last_touch_y = y;
    }
#ifdef __REMOTE_DESKTOP__
    touch_remote = REMOTE_NONE;
  } else {
    stat = touch_remote == REMOTE_PRESS;
#endif
  }

  if (stat != last_touch_status) {
    last_touch_status = stat;
    return stat ? EVT_TOUCH_PRESSED : EVT_TOUCH_RELEASED;
  }
  return stat ? EVT_TOUCH_DOWN : EVT_TOUCH_NONE;
}

void touch_wait_release(void) {
  while (touch_check() != EVT_TOUCH_RELEASED) {
    chThdSleepMilliseconds(TOUCH_RELEASE_POLL_INTERVAL_MS);
  }
}

void touch_get_last_position(int16_t *x, int16_t *y) {
    if(x) *x = last_touch_x;
    if(y) *y = last_touch_y;
}

int touch_is_remote(void) {
#ifdef __REMOTE_DESKTOP__
  return touch_remote != REMOTE_NONE;
#else
  return 0;
#endif
}
