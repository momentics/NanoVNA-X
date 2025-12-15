#include "ch.h"
#include "hal.h"
#include <string.h>
#include <math.h>
#include "nanovna.h"
#include "ui/core/ui_core.h"
#include "ui/core/ui_menu_engine.h" // For ui_draw_button
#include "ui/core/ui_keypad.h"      // For ui_keypad_lever
#include "ui/ui_menu.h"
#include "ui/input/hardware_input.h"
#include "../resources/icons/icons_menu.h"
#include "platform/boards/board_events.h"
#include "ui/menus/menu_system.h" // For lcd_set_brightness

#include "platform/boards/board_events.h"

#include "infra/storage/config_service.h"

#define TOUCH_INTERRUPT_ENABLED 1

static uint8_t touch_status_flag = 0;
// static uint8_t last_touch_status = EVT_TOUCH_NONE;
static uint8_t last_touch_status = 0; // Fixed type to match usage if needed or keep EVT_TOUCH_NONE
int16_t last_touch_x;
int16_t last_touch_y;

static event_bus_t *ui_event_bus = NULL;
static board_events_t *ui_board_events = NULL;
static bool ui_board_events_subscribed = false;
static uint8_t ui_request_flags = UI_CONTROLLER_REQUEST_NONE;

#ifdef REMOTE_DESKTOP
static uint8_t touch_remote = REMOTE_NONE;
#endif

// Mode state
uint8_t ui_mode = UI_NORMAL;

// External handlers (to be defined in other modules)
extern void ui_normal_lever(uint16_t status);
extern void ui_normal_touch(int x, int y);
extern void ui_menu_lever(uint16_t status);
extern void ui_menu_touch(int x, int y);
extern void ui_keypad_lever(uint16_t status);
extern void ui_keypad_touch(int x, int y);
#ifdef SD_FILE_BROWSER
extern void ui_browser_lever(uint16_t status);
extern void ui_browser_touch(int x, int y);
#endif

// Handler table
static const struct {
  void (*button)(uint16_t status);
  void (*touch)(int touch_x, int touch_y);
} UI_HANDLER[] = {
  [UI_NORMAL] = {ui_normal_lever, ui_normal_touch},
  [UI_MENU] = {ui_menu_lever, ui_menu_touch},
  [UI_KEYPAD] = {ui_keypad_lever, ui_keypad_touch},
#ifdef SD_FILE_BROWSER
  [UI_BROWSER] = {ui_browser_lever, ui_browser_touch},
#endif
};

// ====================================================================
// Core UI Controller Logic
// ====================================================================

static void ui_controller_set_request(uint8_t mask) {
  chSysLock();
  ui_request_flags |= mask;
  chSysUnlock();
}

static uint8_t ui_controller_acquire_requests(uint8_t mask) {
  chSysLock();
  uint8_t pending = ui_request_flags & mask;
  ui_request_flags &= (uint8_t)~pending;
  chSysUnlock();
  return pending;
}

uint8_t ui_controller_pending_requests(void) {
  uint8_t flags;
  chSysLock();
  flags = ui_request_flags;
  chSysUnlock();
  if (ui_board_events != NULL) {
    uint32_t pending_mask = board_events_pending_mask(ui_board_events);
    if (pending_mask & (1U << BOARD_EVENT_BUTTON)) {
      flags |= UI_CONTROLLER_REQUEST_LEVER;
    }
    if (pending_mask & (1U << BOARD_EVENT_TOUCH)) {
      flags |= UI_CONTROLLER_REQUEST_TOUCH;
    }
  }
  return flags;
}

void ui_controller_release_requests(uint8_t mask) {
  chSysLock();
  ui_request_flags &= (uint8_t)~mask;
  chSysUnlock();
}

void ui_controller_request_console_break(void) {
  ui_controller_set_request(UI_CONTROLLER_REQUEST_CONSOLE);
}

static void ui_controller_dispatch_board_events(void) {
  if (ui_board_events == NULL) {
    return;
  }
  while (board_events_dispatch(ui_board_events)) {
  }
}

static void ui_controller_on_button_event(const board_event_t *event, void *user_data) {
  (void)user_data;
  (void)event;
  ui_controller_set_request(UI_CONTROLLER_REQUEST_LEVER);
}

static void ui_controller_on_touch_event(const board_event_t *event, void *user_data) {
  (void)user_data;
  (void)event;
  ui_controller_set_request(UI_CONTROLLER_REQUEST_TOUCH);
}

void ui_controller_publish_board_event(board_event_type_t topic, uint16_t channel, bool from_isr) {
  if (ui_board_events == NULL) {
    return;
  }
  board_event_t event = {.topic = topic};
  if (topic == BOARD_EVENT_BUTTON) {
    event.data.button.channel = channel;
  } else {
    event.data.button.channel = channel;
  }
  if (from_isr) {
    board_events_publish_from_isr(ui_board_events, &event);
  } else {
    board_events_publish(ui_board_events, &event);
  }
}

static void ui_on_event(const event_bus_message_t *message, void *user_data) {
  (void)user_data;
  if (message == NULL) {
    return;
  }
  switch (message->topic) {
  case EVENT_SWEEP_STARTED:
    request_to_redraw(REDRAW_BATTERY);
    break;
  case EVENT_SWEEP_COMPLETED:
    request_to_redraw(REDRAW_PLOT | REDRAW_BATTERY);
    break;
  case EVENT_STORAGE_UPDATED:
    request_to_redraw(REDRAW_CAL_STATUS);
    break;
  default:
    break;
  }
}

void ui_attach_event_bus(event_bus_t *bus) {
  if (ui_event_bus == bus) {
    return;
  }
  ui_event_bus = bus;
  if (bus != NULL) {
    event_bus_subscribe(bus, EVENT_SWEEP_STARTED, ui_on_event, NULL);
    event_bus_subscribe(bus, EVENT_SWEEP_COMPLETED, ui_on_event, NULL);
    event_bus_subscribe(bus, EVENT_STORAGE_UPDATED, ui_on_event, NULL);
  }
}

void ui_controller_configure(const ui_controller_port_t *port) {
  if (port == NULL) {
    display_presenter_bind(NULL);
    ui_board_events = NULL;
    ui_board_events_subscribed = false;
    ui_attach_event_bus(NULL);
    return;
  }
  display_presenter_bind(port->display);
  ui_attach_event_bus(port->config_events);
  ui_board_events = port->board_events;
  if (ui_board_events != NULL && !ui_board_events_subscribed) {
    board_events_subscribe(ui_board_events, BOARD_EVENT_BUTTON, ui_controller_on_button_event,
                           NULL);
    board_events_subscribe(ui_board_events, BOARD_EVENT_TOUCH, ui_controller_on_touch_event, NULL);
    ui_board_events_subscribed = true;
  }
}

// ====================================================================
// Low Level Touch Logic
// ====================================================================

#define SOFTWARE_TOUCH
//*******************************************************************************
// Software Touch module
//*******************************************************************************
#ifdef SOFTWARE_TOUCH
static int touch_measure_y(void) {
  // drive low to high on X line (coordinates from top to bottom)
  palClearPad(GPIOB, GPIOB_XN);
  // open Y line (At this state after touch_prepare_sense)
  palSetPadMode(GPIOA, GPIOA_YP, PAL_MODE_INPUT_ANALOG); // <- ADC_TOUCH_Y channel
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
  return adc_single_read(ADC_TOUCH_X);
}
// Manually measure touch event
static inline int touch_status(void) {
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
}

#ifdef REMOTE_DESKTOP
void remote_touch_set(uint16_t state, int16_t x, int16_t y) {
  touch_remote = state;
  if (x != -1)
    last_touch_x = x;
  if (y != -1)
    last_touch_y = y;
  ui_controller_publish_board_event(BOARD_EVENT_TOUCH, 0, false);
}
#endif

void touch_start_watchdog(void) {
  if (touch_status_flag & TOUCH_INTERRUPT_ENABLED)
    return;
  touch_status_flag |= TOUCH_INTERRUPT_ENABLED;
  adc_start_analog_watchdog(); // Watchdog on Y channel
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
static void touch_init(void) {
  // Prepare pin for measure touch event
  touch_prepare_sense();
  // Start touch interrupt, used timer_3 ADC check threshold:
  touch_init_timers();
  touch_start_watchdog(); // Start ADC watchdog (measure by timer 3 interval and trigger interrupt
                          // if touch pressed)
}

// Main software touch function, should:
// set last_touch_x and last_touch_x
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
#ifdef REMOTE_DESKTOP
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
#endif // end SOFTWARE_TOUCH

//*******************************************************************************
//                           UI functions
//*******************************************************************************
void touch_wait_release(void) {
  while (touch_check() != EVT_TOUCH_RELEASED) {
    chThdSleepMilliseconds(TOUCH_RELEASE_POLL_INTERVAL_MS);
  }
}

// Draw button function - Needed by ui_message_box and others
// Should we move this to ui_draw.c? keeping it here for now or assuming linked.
// It uses lcd_ functions which are macros in ui_controller.c to display_presenter.
// We need those macros here too.
// I'll copy the macros.

// Re-define for core UI use if needed, but undef first to avoid warning
#undef lcd_drawstring
#undef LCD_DRAWSTRING
#define LCD_DRAWSTRING(...) display_presenter_drawstring(__VA_ARGS__)
#define LCD_DRAWSTRING_SIZE(...) display_presenter_drawstring_size(__VA_ARGS__)
#define LCD_PRINTF(...) display_presenter_printf(__VA_ARGS__)
#define LCD_DRAWCHAR(...) display_presenter_drawchar(__VA_ARGS__)
#define LCD_DRAWCHAR_SIZE(...) display_presenter_drawchar_size(__VA_ARGS__)
#define LCD_DRAWFONT(...) display_presenter_drawfont(__VA_ARGS__)
#define LCD_FILL(...) display_presenter_fill(__VA_ARGS__)
#define LCD_BULK(...) display_presenter_bulk(__VA_ARGS__)
#define LCD_READ_MEMORY(...) display_presenter_read_memory(__VA_ARGS__)
#define LCD_LINE(...) display_presenter_line(__VA_ARGS__)
#define LCD_SET_BACKGROUND(...) display_presenter_set_background(__VA_ARGS__)
#define LCD_SET_COLORS(...) display_presenter_set_colors(__VA_ARGS__)
#define LCD_SET_FLIP(...) display_presenter_set_flip(__VA_ARGS__)
#undef LCD_SET_FONT
#define LCD_SET_FONT(...) display_presenter_set_font(__VA_ARGS__)
#define LCD_BLIT_BITMAP(...) display_presenter_blit_bitmap(__VA_ARGS__)

void ui_draw_button(uint16_t x, uint16_t y, uint16_t w, uint16_t h, button_t *b) {
  uint16_t type = b->border;
  uint16_t bw = type & BUTTON_BORDER_WIDTH_MASK;
  // Draw border if width > 0
  if (bw) {
    uint16_t br = LCD_RISE_EDGE_COLOR;
    uint16_t bd = LCD_FALLEN_EDGE_COLOR;
    LCD_SET_BACKGROUND(type & BUTTON_BORDER_TOP ? br : bd);
    LCD_FILL(x, y, w, bw); // top
    LCD_SET_BACKGROUND(type & BUTTON_BORDER_LEFT ? br : bd);
    LCD_FILL(x, y, bw, h); // left
    LCD_SET_BACKGROUND(type & BUTTON_BORDER_RIGHT ? br : bd);
    LCD_FILL(x + w - bw, y, bw, h); // right
    LCD_SET_BACKGROUND(type & BUTTON_BORDER_BOTTOM ? br : bd);
    LCD_FILL(x, y + h - bw, w, bw); // bottom
  }
  // Set colors for button and text
  LCD_SET_COLORS(b->fg, b->bg);
  if (type & BUTTON_BORDER_NO_FILL)
    return;
  LCD_FILL(x + bw, y + bw, w - (bw * 2), h - (bw * 2));
}

void ui_message_box_draw(const char *header, const char *text) {
  button_t b;
  int x, y;
  b.bg = LCD_MENU_COLOR;
  b.fg = LCD_MENU_TEXT_COLOR;
  b.border = BUTTON_BORDER_FLAT;
  if (header) { // Draw header
    ui_draw_button((LCD_WIDTH - MESSAGE_BOX_WIDTH) / 2, LCD_HEIGHT / 2 - 40, MESSAGE_BOX_WIDTH, 60,
                   &b);
    x = (LCD_WIDTH - MESSAGE_BOX_WIDTH) / 2 + 10;
    y = LCD_HEIGHT / 2 - 40 + 5;
    LCD_DRAWSTRING(x, y, header);
    request_to_redraw(REDRAW_AREA);
  }
  if (text) { // Draw window
    LCD_SET_COLORS(LCD_MENU_TEXT_COLOR, LCD_FG_COLOR);
    LCD_FILL((LCD_WIDTH - MESSAGE_BOX_WIDTH) / 2 + 3, LCD_HEIGHT / 2 - 40 + FONT_STR_HEIGHT + 8,
             MESSAGE_BOX_WIDTH - 6, 60 - FONT_STR_HEIGHT - 8 - 3);
    x = (LCD_WIDTH - MESSAGE_BOX_WIDTH) / 2 + 20;
    y = LCD_HEIGHT / 2 - 40 + FONT_STR_HEIGHT + 8 + 14;
    LCD_DRAWSTRING(x, y, text);
    request_to_redraw(REDRAW_AREA);
  }
}

void ui_message_box(const char *header, const char *text, uint32_t delay) {
  ui_message_box_draw(header, text);

  do {
    chThdSleepMilliseconds(delay == 0 ? 50 : delay);
  } while (delay == 0 && ui_input_check() != EVT_BUTTON_SINGLE_CLICK &&
           touch_check() != EVT_TOUCH_PRESSED);
}

// Touch calibration
// Includes touch_bitmap - need to verify if available.
// It was in ui_controller.c... included via #include "icon_menu.h" ? No.
// Let's assume touch_bitmap is available or include it.
// In ui_controller.c: extern const uint8_t touch_bitmap[]; (found via get_touch_point in
// ui_controller.c?) I need `get_touch_point` too.

// Touch bitmap definition is likely in a resource file.
// I'll implement get_touch_point here.
extern const uint8_t TOUCH_BITMAP[]; // Assuming it exists or I need to find where it is defined.

static void get_touch_point(uint16_t x, uint16_t y, const char *name, int16_t *data) {
  // Clear screen and ask for press
  LCD_SET_COLORS(LCD_FG_COLOR, LCD_BG_COLOR);
  lcd_clear_screen();
  LCD_BLIT_BITMAP(x, y, TOUCH_MARK_W, TOUCH_MARK_H, (const uint8_t *)TOUCH_BITMAP);
  LCD_PRINTF((LCD_WIDTH - FONT_STR_WIDTH(18)) / 2, (LCD_HEIGHT - FONT_GET_HEIGHT) / 2, "TOUCH %s *",
             name);
  // Wait release, and fill data
  touch_wait_release();
  data[0] = last_touch_x;
  data[1] = last_touch_y;
}

void ui_touch_cal_exec(void) {
  const uint16_t x1 = CALIBRATION_OFFSET - TOUCH_MARK_X;
  const uint16_t y1 = CALIBRATION_OFFSET - TOUCH_MARK_Y;
  const uint16_t x2 = LCD_WIDTH - 1 - CALIBRATION_OFFSET - TOUCH_MARK_X;
  const uint16_t y2 = LCD_HEIGHT - 1 - CALIBRATION_OFFSET - TOUCH_MARK_Y;
  uint16_t p1 = 0, p2 = 2;
#ifdef FLIP_DISPLAY
  if (VNA_MODE(VNA_MODE_FLIP_DISPLAY)) {
    p1 = 2, p2 = 0;
  }
#endif
  get_touch_point(x1, y1, "UPPER LEFT", &config._touch_cal[p1]);
  get_touch_point(x2, y2, "LOWER RIGHT", &config._touch_cal[p2]);
  config_service_notify_configuration_changed();
}

void touch_position(int *x, int *y) {
#ifdef REMOTE_DESKTOP
  if (touch_remote != REMOTE_NONE) {
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
  if (tx < 0) {
    tx = 0;
  } else if (tx >= LCD_WIDTH) {
    tx = LCD_WIDTH - 1;
  }

  ty = (int)(((int64_t)scale_y * (last_touch_y - config._touch_cal[1])) >> 16) + CALIBRATION_OFFSET;
  if (ty < 0) {
    ty = 0;
  } else if (ty >= LCD_HEIGHT) {
    ty = LCD_HEIGHT - 1;
  }

#ifdef FLIP_DISPLAY
  if (VNA_MODE(VNA_MODE_FLIP_DISPLAY)) {
    tx = LCD_WIDTH - 1 - tx;
    ty = LCD_HEIGHT - 1 - ty;
  }
#endif
  *x = tx;
  *y = ty;
}

void ui_touch_draw_test(void) {
  int x0, y0;
  int x1, y1;
  LCD_SET_COLORS(LCD_FG_COLOR, LCD_BG_COLOR);
  lcd_clear_screen();
  LCD_DRAWSTRING(OFFSETX, LCD_HEIGHT - FONT_GET_HEIGHT,
                 "TOUCH TEST: DRAG PANEL, PRESS BUTTON TO FINISH");

  while (1) {
    if (ui_input_check() & EVT_BUTTON_SINGLE_CLICK)
      break;
    if (touch_check() == EVT_TOUCH_PRESSED) {
      touch_position(&x0, &y0);
      do {
        LCD_PRINTF(10, 30, "%3d %3d ", x0, y0);
        chThdSleepMilliseconds(50);
        touch_position(&x1, &y1);
        LCD_LINE(x0, y0, x1, y1);
        x0 = x1;
        y0 = y1;
      } while (touch_check() != EVT_TOUCH_RELEASED);
    }
  }
}

// Process loop
static void ui_process_lever(void) {
  uint16_t status = ui_input_check();
  if (status)
    UI_HANDLER[ui_mode].button(status);
}

static void ui_process_touch(void) {
  int touch_x, touch_y;
  int status = touch_check();
  if (status == EVT_TOUCH_PRESSED || status == EVT_TOUCH_DOWN) {
    touch_position(&touch_x, &touch_y);
    UI_HANDLER[ui_mode].touch(touch_x, touch_y);
  }
}

void ui_process(void) {
  ui_controller_dispatch_board_events();
  uint8_t requests =
    ui_controller_acquire_requests(UI_CONTROLLER_REQUEST_LEVER | UI_CONTROLLER_REQUEST_TOUCH);
  if (requests & UI_CONTROLLER_REQUEST_LEVER) {
    ui_process_lever();
  }
  if (requests & UI_CONTROLLER_REQUEST_TOUCH) {
    ui_process_touch();
  }

  touch_start_watchdog();
}

void handle_button_interrupt(uint16_t channel) {
  ui_controller_publish_board_event(BOARD_EVENT_BUTTON, channel, true);
}

void handle_touch_interrupt(void) {
  ui_controller_publish_board_event(BOARD_EVENT_TOUCH, 0, true);
}

#if HAL_USE_EXT == TRUE
static void handle_button_ext(EXTDriver *extp, expchannel_t channel) {
  (void)extp;
  handle_button_interrupt((uint16_t)channel);
}

static const EXTConfig extcfg = {
  {{EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_RISING_EDGE | EXT_CH_MODE_AUTOSTART | EXT_MODE_GPIOA, handle_button_ext}, // EXT1
   {EXT_CH_MODE_RISING_EDGE | EXT_CH_MODE_AUTOSTART | EXT_MODE_GPIOA, handle_button_ext}, // EXT2
   {EXT_CH_MODE_RISING_EDGE | EXT_CH_MODE_AUTOSTART | EXT_MODE_GPIOA, handle_button_ext}, // EXT3
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL},
   {EXT_CH_MODE_DISABLED, NULL}}};

static void ui_init_ext(void) {
  extStart(&EXTD1, &extcfg);
}
#else
static void ui_init_ext(void) {
  extStart();
  ext_channel_enable(1, EXT_CH_MODE_RISING_EDGE | EXT_MODE_GPIOA);
  ext_channel_enable(2, EXT_CH_MODE_RISING_EDGE | EXT_MODE_GPIOA);
  ext_channel_enable(3, EXT_CH_MODE_RISING_EDGE | EXT_MODE_GPIOA);
}
#endif

// We need to implement ui_mode_ functions locally but they might call menu engine
extern void menu_stack_reset(void); // Will be in ui_menu_engine.c or ui_controller.c?
// menu_stack_reset is static in ui_controller.c but ui_init calls it.
// I should move menu_stack_reset to ui_menu_engine.c and expose it.

void ui_init(void) {
  menu_stack_reset();
  ui_input_reset_state();
  ui_init_ext();
  touch_init();
#ifdef LCD_BRIGHTNESS
  lcd_set_brightness(config._brightness);
#endif
}
