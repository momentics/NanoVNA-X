#include "ch.h"
#include "hal.h"
#include "nanovna.h"
#include "ui/core/ui_core.h"
#include "ui/core/ui_keypad.h"
#include "ui/display/display_presenter.h"
#include "ui/input/hardware_input.h"
#include <string.h>

// Macros for drawing
#undef lcd_drawstring
#define lcd_drawstring(...) display_presenter_drawstring(__VA_ARGS__)
#define lcd_drawstring_size(...) display_presenter_drawstring_size(__VA_ARGS__)
#define lcd_printf(...) display_presenter_printf(__VA_ARGS__)
#define lcd_drawchar(...) display_presenter_drawchar(__VA_ARGS__)
#define lcd_drawchar_size(...) display_presenter_drawchar_size(__VA_ARGS__)
#define lcd_drawfont(...) display_presenter_drawfont(__VA_ARGS__)
#define lcd_fill(...) display_presenter_fill(__VA_ARGS__)
#define lcd_set_colors(...) display_presenter_set_colors(__VA_ARGS__)

extern const keypads_list keypads_mode_tbl[];

enum { NUM_KEYBOARD, TXT_KEYBOARD };

typedef struct {
  uint16_t x_offs;
  uint16_t y_offs;
  uint16_t width;
  uint16_t height;
} keypad_pos_t;

// Keyboard size and position data
static const keypad_pos_t key_pos[] = {
    [NUM_KEYBOARD] = {KP_X_OFFSET, KP_Y_OFFSET, KP_WIDTH, KP_HEIGHT},
    [TXT_KEYBOARD] = {KPF_X_OFFSET, KPF_Y_OFFSET, KPF_WIDTH, KPF_HEIGHT}};

// Key definitions
#define KP_EMPTY 0xFF
#define KP_0 0
#define KP_1 1
#define KP_2 2
#define KP_3 3
#define KP_4 4
#define KP_5 5
#define KP_6 6
#define KP_7 7
#define KP_8 8
#define KP_9 9
#define KP_PERIOD 10
// Index 11 is unused/mystery
#define KP_MINUS 12
#define KP_BS 13
#define KP_k 14
#define KP_M 15
#define KP_G 16
#define KP_m 17
#define KP_u 18
#define KP_n 19
#define KP_p 20
#define KP_X1 21
#define KP_ENTER 22
#define KP_PERCENT 23

typedef struct {
  uint8_t size, type;
  struct {
    uint8_t pos;
    uint8_t c;
  } buttons[];
} keypads_t;

static const keypads_t keypads_freq[] = {
    {16, NUM_KEYBOARD},               // 16 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9 G
    {0x02, KP_1},                     // 4 5 6 M
    {0x12, KP_2},                     // 1 2 3 k
    {0x22, KP_3},                     // 0 . < x
    {0x01, KP_4},       {0x11, KP_5}, {0x21, KP_6}, {0x00, KP_7},  {0x10, KP_8}, {0x20, KP_9},
    {0x30, KP_G},       {0x31, KP_M}, {0x32, KP_k}, {0x33, KP_X1}, {0x23, KP_BS}};

static const keypads_t keypads_ufloat[] = {
    //
    {16, NUM_KEYBOARD},               // 13 + 3 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6
    {0x12, KP_2},                     // 1 2 3
    {0x22, KP_3},                     // 0 . < x
    {0x01, KP_4},       {0x11, KP_5},     {0x21, KP_6},     {0x00, KP_7},
    {0x10, KP_8},       {0x20, KP_9},     {0x33, KP_ENTER}, {0x23, KP_BS},
    {0x30, KP_EMPTY},   {0x31, KP_EMPTY}, {0x32, KP_EMPTY},
};

static const keypads_t keypads_percent[] = {
    //
    {16, NUM_KEYBOARD},               // 13 + 3 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6
    {0x12, KP_2},                     // 1 2 3
    {0x22, KP_3},                     // 0 . < %
    {0x01, KP_4},       {0x11, KP_5},     {0x21, KP_6},       {0x00, KP_7},
    {0x10, KP_8},       {0x20, KP_9},     {0x33, KP_PERCENT}, {0x23, KP_BS},
    {0x30, KP_EMPTY},   {0x31, KP_EMPTY}, {0x32, KP_EMPTY},
};

static const keypads_t keypads_float[] = {
    {16, NUM_KEYBOARD},               // 14 + 2 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6
    {0x12, KP_2},                     // 1 2 3 -
    {0x22, KP_3},                     // 0 . < x
    {0x01, KP_4},       {0x11, KP_5},     {0x21, KP_6},     {0x00, KP_7},
    {0x10, KP_8},       {0x20, KP_9},     {0x32, KP_MINUS}, {0x33, KP_ENTER},
    {0x23, KP_BS},      {0x30, KP_EMPTY}, {0x31, KP_EMPTY},
};

static const keypads_t keypads_mfloat[] = {{16, NUM_KEYBOARD}, // 16 buttons NUM keyboard (4x4 size)
                                           {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9 u
                                           {0x02, KP_1},                     // 4 5 6 m
                                           {0x12, KP_2},                     // 1 2 3 -
                                           {0x22, KP_3},                     // 0 . < x
                                           {0x01, KP_4},       {0x11, KP_5}, {0x21, KP_6},
                                           {0x00, KP_7},       {0x10, KP_8}, {0x20, KP_9},
                                           {0x30, KP_u},       {0x31, KP_m}, {0x32, KP_MINUS},
                                           {0x33, KP_ENTER},   {0x23, KP_BS}};

static const keypads_t keypads_mkufloat[] = {
    {16, NUM_KEYBOARD},               // 15 + 1 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9
    {0x02, KP_1},                     // 4 5 6 m
    {0x12, KP_2},                     // 1 2 3 k
    {0x22, KP_3},                     // 0 . < x
    {0x01, KP_4},       {0x11, KP_5}, {0x21, KP_6},  {0x00, KP_7},  {0x10, KP_8},     {0x20, KP_9},
    {0x31, KP_m},       {0x32, KP_k}, {0x33, KP_X1}, {0x23, KP_BS}, {0x30, KP_EMPTY},
};

static const keypads_t keypads_nfloat[] = {
    {16, NUM_KEYBOARD},               // 16 buttons NUM keyboard (4x4 size)
    {0x13, KP_PERIOD},  {0x03, KP_0}, // 7 8 9 u
    {0x02, KP_1},                     // 4 5 6 n
    {0x12, KP_2},                     // 1 2 3 p
    {0x22, KP_3},                     // 0 . < -
    {0x01, KP_4},       {0x11, KP_5}, {0x21, KP_6}, {0x00, KP_7},     {0x10, KP_8}, {0x20, KP_9},
    {0x30, KP_u},       {0x31, KP_n}, {0x32, KP_p}, {0x33, KP_MINUS}, {0x23, KP_BS}};

// QWERTY keyboard
static const keypads_t keypads_text[] = {
    {40, TXT_KEYBOARD}, // 40 buttons TXT keyboard (10x4 size)
    {0x00, '1'},        {0x10, '2'}, {0x20, '3'}, {0x30, '4'},         {0x40, '5'},
    {0x50, '6'},        {0x60, '7'}, {0x70, '8'}, {0x80, '9'},         {0x90, '0'},
    {0x01, 'Q'},        {0x11, 'W'}, {0x21, 'E'}, {0x31, 'R'},         {0x41, 'T'},
    {0x51, 'Y'},        {0x61, 'U'}, {0x71, 'I'}, {0x81, 'O'},         {0x91, 'P'},
    {0x02, 'A'},        {0x12, 'S'}, {0x22, 'D'}, {0x32, 'F'},         {0x42, 'G'},
    {0x52, 'H'},        {0x62, 'J'}, {0x72, 'K'}, {0x82, 'L'},         {0x92, '_'},
    {0x03, '-'},        {0x13, 'Z'}, {0x23, 'X'}, {0x33, 'C'},         {0x43, 'V'},
    {0x53, 'B'},        {0x63, 'N'}, {0x73, 'M'}, {0x83, S_LARROW[0]}, {0x93, S_ENTER[0]},
};

static const keypads_t* keypad_type_list[] = {
    [KEYPAD_FREQ] = keypads_freq,         // frequency input
    [KEYPAD_UFLOAT] = keypads_ufloat,     // unsigned float input
    [KEYPAD_PERCENT] = keypads_percent,   // unsigned float input in percent
    [KEYPAD_FLOAT] = keypads_float,       //   signed float input
    [KEYPAD_MFLOAT] = keypads_mfloat,     //   signed milli/micro float input
    [KEYPAD_MKUFLOAT] = keypads_mkufloat, // unsigned milli/kilo float input
    [KEYPAD_NFLOAT] = keypads_nfloat,     //   signed micro/nano/pico float input
    [KEYPAD_TEXT] = keypads_text          // text input
};

#if NUMINPUT_LEN + 2 > TXTINPUT_LEN + 1
char kp_buf[NUMINPUT_LEN + 2];
#else
char kp_buf[TXTINPUT_LEN + 1];
#endif

// State
static const keypads_t* keypads;
uint8_t keypad_mode;

// Helper function to get line count
static int get_lines_count(const char* label) {
  int n = 1;
  while (*label)
    if (*label++ == '\n')
      n++;
  return n;
}

// Get value from keyboard functions
// Assuming my_atof etc are available via nanovna.h
extern float my_atof(const char* s);
extern int32_t my_atoi(const char* s);
extern uint32_t my_atoui(const char* s);

float keyboard_get_float(void) {
  return my_atof(kp_buf);
}
freq_t keyboard_get_freq(void) {
  return my_atoui(kp_buf);
}
uint32_t keyboard_get_uint(void) {
  return my_atoui(kp_buf);
}
int32_t keyboard_get_int(void) {
  return my_atoi(kp_buf);
}

// Drawing logic
static void keypad_draw_button(int id) {
  // Use extern selection from ui_menu_engine.h?
  // ui_keypad.c has its own selection?
  // In ui_controller.c selection was shared but menu_draw and keypad used it.
  // ui_menu_engine exports selection.
  extern int8_t selection;

  if (id < 0)
    return;
  button_t button;
  button.fg = LCD_MENU_TEXT_COLOR;

  if (id == selection) {
    button.bg = LCD_MENU_ACTIVE_COLOR;
    button.border = KEYBOARD_BUTTON_BORDER | BUTTON_BORDER_FALLING;
  } else {
    button.bg = LCD_MENU_COLOR;
    button.border = KEYBOARD_BUTTON_BORDER | BUTTON_BORDER_RISE;
  }

  const keypad_pos_t* p = &key_pos[keypads->type];
  int x = p->x_offs + (keypads->buttons[id].pos >> 4) * p->width;
  int y = p->y_offs + (keypads->buttons[id].pos & 0xF) * p->height;
  ui_draw_button(x, y, p->width, p->height, &button);
  uint8_t ch = keypads->buttons[id].c;
  if (ch == KP_EMPTY)
    return;
  if (keypads->type == NUM_KEYBOARD) {
    lcd_drawfont(ch, x + (KP_WIDTH - NUM_FONT_GET_WIDTH) / 2,
                 y + (KP_HEIGHT - NUM_FONT_GET_HEIGHT) / 2);
  } else {
#if 0
    lcd_drawchar(ch,
                     x + (KPF_WIDTH - FONT_WIDTH) / 2,
                     y + (KPF_HEIGHT - FONT_GET_HEIGHT) / 2);
#else
    lcd_drawchar_size(ch, x + KPF_WIDTH / 2 - FONT_WIDTH + 1, y + KPF_HEIGHT / 2 - FONT_GET_HEIGHT,
                      2);
#endif
  }
}

static void draw_keypad(void) {
  for (int i = 0; i < keypads->size; i++)
    keypad_draw_button(i);
}

static int period_pos(void) {
  int j;
  for (j = 0; kp_buf[j] && kp_buf[j] != '.'; j++)
    ;
  return j;
}

static void draw_numeric_area_frame(void) {
  lcd_set_colors(LCD_INPUT_TEXT_COLOR, LCD_INPUT_BG_COLOR);
  lcd_fill(0, LCD_HEIGHT - NUM_INPUT_HEIGHT, LCD_WIDTH, NUM_INPUT_HEIGHT);
  const char* label = keypads_mode_tbl[keypad_mode].name;
  int lines = get_lines_count(label);
  lcd_drawstring(10, LCD_HEIGHT - (FONT_STR_HEIGHT * lines + NUM_INPUT_HEIGHT) / 2, label);
}

static void draw_numeric_input(const char* buf) {
  uint16_t x = 14 + FONT_STR_WIDTH(12), space;
  uint16_t y = LCD_HEIGHT - (NUM_FONT_GET_HEIGHT + NUM_INPUT_HEIGHT) / 2;
  uint16_t xsim;
#ifdef __USE_RTC__
  if ((1 << keypad_mode) & ((1 << KM_RTC_DATE) | (1 << KM_RTC_TIME)))
    xsim = 0b01010100;
  else
#endif
    xsim = (0b00100100100100100 >> (2 - (period_pos() % 3))) & (~1);
  lcd_set_colors(LCD_INPUT_TEXT_COLOR, LCD_INPUT_BG_COLOR);
  while (*buf) {
    int c = *buf++;
    if (c == '.') {
      c = KP_PERIOD;
      xsim <<= 4;
    } else if (c == '-') {
      c = KP_MINUS;
      xsim &= ~3;
    } else if (c >= '0' && c <= '9')
      c -= '0';
    else
      continue;
    // Add space before char
    space = 2 + 10 * (xsim & 1);
    xsim >>= 1;
    lcd_fill(x, y, space, NUM_FONT_GET_HEIGHT);
    x += space;
    lcd_drawfont(c, x, y);
    x += NUM_FONT_GET_WIDTH;
  }
  lcd_fill(x, y, NUM_FONT_GET_WIDTH + 2 + 10, NUM_FONT_GET_HEIGHT);
}

static void draw_text_input(const char* buf) {
  lcd_set_colors(LCD_INPUT_TEXT_COLOR, LCD_INPUT_BG_COLOR);
#if 0
  uint16_t x = 14 + FONT_STR_WIDTH(5);
  uint16_t y = LCD_HEIGHT-(FONT_GET_HEIGHT + NUM_INPUT_HEIGHT)/2;
  lcd_fill(x, y, FONT_STR_WIDTH(20), FONT_GET_HEIGHT);
  lcd_printf(x, y, buf);
#else
  int n = 2;
  uint16_t x = 14 + FONT_STR_WIDTH(5);
  uint16_t y = LCD_HEIGHT - (FONT_GET_HEIGHT * n + NUM_INPUT_HEIGHT) / 2;
  lcd_fill(x, y, FONT_STR_WIDTH(20) * n, FONT_GET_HEIGHT * n);
  lcd_drawstring_size(buf, x, y, n);
#endif
}

// Processing
enum { K_CONTINUE = 0, K_DONE, K_CANCEL };
static int num_keypad_click(int c, int kp_index) {
  if (c >= KP_k && c <= KP_PERCENT) {
    if (kp_index == 0)
      return K_CANCEL;
    if (c >= KP_k && c <= KP_G) { // Apply k, M, G input (add zeroes and shift . right)
      uint16_t scale = c - KP_k + 1;
      scale += (scale << 1);
      int i = period_pos();
      if (scale + i > NUMINPUT_LEN)
        scale = NUMINPUT_LEN - i;
      do {
        char v = kp_buf[i + 1];
        if (v == 0 || kp_buf[i] == 0) {
          v = '0';
          kp_buf[i + 2] = 0;
        }
        kp_buf[i + 1] = kp_buf[i];
        kp_buf[i++] = v;
      } while (--scale);
    } else if (c >= KP_m &&
               c <= KP_p) { // Apply m, u, n, p input (add format at end for atof function)
      const char prefix[] = {'m', 'u', 'n', 'p'};
      kp_buf[kp_index] = prefix[c - KP_m];
      kp_buf[kp_index + 1] = 0;
    }
    return K_DONE;
  }
#ifdef __USE_RTC__
  int maxlength = (1 << keypad_mode) & ((1 << KM_RTC_DATE) | (1 << KM_RTC_TIME)) ? 6 : NUMINPUT_LEN;
#else
  int maxlength = NUMINPUT_LEN;
#endif
  if (c == KP_BS) {
    if (kp_index == 0)
      return K_CANCEL;
    --kp_index;
  } else if (c == KP_MINUS) {
    int i;
    if (kp_buf[0] == '-') {
      for (i = 0; i < NUMINPUT_LEN; i++)
        kp_buf[i] = kp_buf[i + 1];
      --kp_index;
    } else {
      for (i = NUMINPUT_LEN; i > 0; i--)
        kp_buf[i] = kp_buf[i - 1];
      kp_buf[0] = '-';
      if (kp_index < maxlength)
        ++kp_index;
    }
  } else if (kp_index < maxlength) {
    if (c <= KP_9)
      kp_buf[kp_index++] = '0' + c;
    else if (c == KP_PERIOD && kp_index == period_pos() &&
             maxlength == NUMINPUT_LEN) // append period if there are no period and for num input
                                        // (skip for date/time)
      kp_buf[kp_index++] = '.';
  }
  kp_buf[kp_index] = '\0';
  draw_numeric_input(kp_buf);
  return K_CONTINUE;
}

static int txt_keypad_click(int c, int kp_index) {
  if (c == S_ENTER[0]) { // Enter
    return kp_index == 0 ? K_CANCEL : K_DONE;
  }
  if (c == S_LARROW[0]) { // Backspace
    if (kp_index == 0)
      return K_CANCEL;
    --kp_index;
  } else if (kp_index < TXTINPUT_LEN) { // any other text input
    kp_buf[kp_index++] = c;
  }
  kp_buf[kp_index] = '\0';
  draw_text_input(kp_buf);
  return K_CONTINUE;
}

static void keypad_click(int key) {
  int c = keypads->buttons[key].c;
  int index = strlen(kp_buf);
  int result =
      keypads->type == NUM_KEYBOARD ? num_keypad_click(c, index) : txt_keypad_click(c, index);
  if (result == K_DONE)
    ui_keyboard_cb(keypad_mode, NULL); // apply input done
  // Exit loop on done or cancel
  if (result != K_CONTINUE)
    ui_mode_normal();
}

void ui_mode_keypad(int mode) {
  if (ui_mode == UI_KEYPAD)
    return;
  ui_mode = UI_KEYPAD;
  set_area_size(0, 0);
  // keypads array
  keypad_mode = mode;
  keypads = keypad_type_list[keypads_mode_tbl[mode].keypad_type];
  extern int8_t selection;
  selection = -1;
  kp_buf[0] = 0;
  //  menu_draw(-1);
  draw_keypad();
  draw_numeric_area_frame();
}

void ui_keypad_touch(int touch_x, int touch_y) {
  const keypad_pos_t* p = &key_pos[keypads->type];
  if (touch_x < p->x_offs || touch_y < p->y_offs)
    return;
  // Calculate key position from touch x and y
  touch_x -= p->x_offs;
  touch_x /= p->width;
  touch_y -= p->y_offs;
  touch_y /= p->height;
  uint8_t pos = (touch_y & 0x0F) | (touch_x << 4);
  extern int8_t selection;

  for (int i = 0; i < keypads->size; i++) {
    if (keypads->buttons[i].pos != pos)
      continue;
    if (keypads->buttons[i].c == KP_EMPTY)
      break;
    int old = selection;
    keypad_draw_button(selection = i); // draw new focus
    keypad_draw_button(old);           // Erase old focus
    touch_wait_release();
    selection = -1;
    keypad_draw_button(i); // erase new focus
    keypad_click(i);       // Process input
    return;
  }
  return;
}

void ui_keypad_lever(uint16_t status) {
  extern int8_t selection;
  if (status == EVT_BUTTON_SINGLE_CLICK) {
    if (selection >= 0) // Process input
      keypad_click(selection);
    return;
  }
  int keypads_last_index = keypads->size - 1;
  do {
    int old = selection;
    do {
      if ((status & EVT_DOWN) && --selection < 0)
        selection = keypads_last_index;
      if ((status & EVT_UP) && ++selection > keypads_last_index)
        selection = 0;
    } while (keypads->buttons[selection].c == KP_EMPTY); // Skip empty
    keypad_draw_button(old);
    keypad_draw_button(selection);
    chThdSleepMilliseconds(100);
  } while ((status = ui_input_wait_release()) != 0);
}
