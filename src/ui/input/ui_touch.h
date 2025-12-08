#pragma once

#include <stdint.h>

#define EVT_TOUCH_NONE 0
#define EVT_TOUCH_DOWN 1
#define EVT_TOUCH_PRESSED 2
#define EVT_TOUCH_RELEASED 3
#define EVT_TOUCH_DOUBLE_CLICK 8

void touch_position(int* x, int* y);
void touch_init(void);
int touch_check(void);
void touch_wait_release(void);
void touch_start_watchdog(void);
void touch_stop_watchdog(void);
void touch_get_last_position(int16_t *x, int16_t *y);

int touch_is_remote(void);

#ifdef __REMOTE_DESKTOP__
void remote_touch_set(uint16_t state, int16_t x, int16_t y);
#endif
