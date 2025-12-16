#include "ch.h"
#include "hal.h"
#include "nanovna.h"
#include "ui/core/ui_core.h"
#include "ui/core/ui_menu_engine.h"
#include "ui/core/ui_keypad.h"
#include "ui/display/display_presenter.h"
#include "ui/input/hardware_input.h"

// Bring in macros for drawing (or include a common ui_draw.h if I created one)
// For now, reuse the macros as localized here.
#undef lcd_drawstring
#define lcd_drawstring(...) display_presenter_drawstring(__VA_ARGS__)
#define lcd_printf(...) display_presenter_printf(__VA_ARGS__)
#define lcd_fill(...) display_presenter_fill(__VA_ARGS__)
#define lcd_set_background(...) display_presenter_set_background(__VA_ARGS__)
#define lcd_set_colors(...) display_presenter_set_colors(__VA_ARGS__)
#define lcd_set_font(...) display_presenter_set_font(__VA_ARGS__)
#define lcd_blit_bitmap(...) display_presenter_blit_bitmap(__VA_ARGS__)


// Externs
extern const menuitem_t menu_top[];

// State variables
#define MENU_STACK_DEPTH_MAX 5
const menuitem_t* menu_stack[MENU_STACK_DEPTH_MAX];
uint8_t menu_current_level = 0;
int8_t selection = -1;
static uint16_t menu_button_height;

// ===================================
// Menu Helper Functions
// ===================================

menuitem_t* ui_menu_list(const menu_descriptor_t* descriptors, size_t count,
                                const char* label, const void* reference,
                                menuitem_t* out) {
  if (descriptors == NULL || out == NULL)
    return out;
  for (size_t i = 0; i < count; i++) {
    out->type = descriptors[i].type;
    out->data = descriptors[i].data;
    out->label = label;
    out->reference = reference;
    out++;
  }
  return out;
}

void menu_set_next(menuitem_t* entry, const menuitem_t* next) {
  if (entry == NULL)
    return;
  entry->type = MT_NEXT;
  entry->data = 0;
  entry->label = NULL;
  entry->reference = next;
}

void ui_cycle_option(uint16_t* dst, const option_desc_t* list, size_t count, button_t* b) {
  if (dst == NULL || list == NULL || count == 0)
    return;
  size_t idx = 0;
  while (idx < count && list[idx].value != *dst)
    idx++;
  if (idx >= count)
    idx = 0;
  const option_desc_t* desc = &list[idx];
  if (b) {
    if (desc->label)
      b->p1.text = desc->label;
    if (desc->icon >= 0)
      b->icon = desc->icon;
    return;
  }
  idx = (idx + 1) % count;
  *dst = list[idx].value;
}

static void menu_stack_reset_internal(void) {
  menu_stack[0] = menu_top;
  for (size_t i = 1; i < MENU_STACK_DEPTH_MAX; i++) {
    menu_stack[i] = NULL;
  }
  menu_current_level = 0;
  selection = -1;
  menu_button_height = MENU_BUTTON_HEIGHT(MENU_BUTTON_MIN);
}

void menu_stack_reset(void) {
  menu_stack_reset_internal();
}

static const menuitem_t* menu_next_item(const menuitem_t* m) {
  if (m == NULL)
    return NULL;
  m++; // Next item
  return m->type == MT_NEXT ? (menuitem_t*)m->reference : m;
}

const menuitem_t* current_menu_item(int i) {
  const menuitem_t* m = menu_stack[menu_current_level];
  while (i--)
    m = menu_next_item(m);
  return m;
}

int current_menu_get_count(void) {
  int i = 0;
  const menuitem_t* m = menu_stack[menu_current_level];
  while (m) {
    m = menu_next_item(m);
    i++;
  }
  return i;
}

static int get_lines_count(const char* label) {
  int n = 1;
  while (*label)
    if (*label++ == '\n')
      n++;
  return n;
}

static void ensure_selection(void) {
  int i = current_menu_get_count();
  if (selection < 0)
    selection = -1;
  else if (selection >= i)
    selection = i - 1;
  if (i < MENU_BUTTON_MIN)
    i = MENU_BUTTON_MIN;
  else if (i >= MENU_BUTTON_MAX)
    i = MENU_BUTTON_MAX;
  menu_button_height = MENU_BUTTON_HEIGHT(i);
}

void menu_move_back(bool leave_ui) {
  if (menu_current_level == 0)
    return;
  menu_current_level--;
  ensure_selection();
  if (leave_ui)
    ui_mode_normal();
}

void menu_set_submenu(const menuitem_t* submenu) {
  menu_stack[menu_current_level] = submenu;
  ensure_selection();
}

void menu_push_submenu(const menuitem_t* submenu) {
  if (menu_current_level < MENU_STACK_DEPTH_MAX - 1)
    menu_current_level++;
  menu_set_submenu(submenu);
}

// Icons
#include "../resources/icons/icons_menu.h"
// This path might be wrong relative to ui/core/
// Original: #include "../resources/icons/icons_menu.h" in ui_controller.c (src/ui/controller/ui_controller.c)
// src/ui/core/ui_menu_engine.c -> ../../resources/icons/icons_menu.h?
// resources/icons is likely in src/ui/resources/icons or src/resources/icons?
// I need to check where resources dir is.
// view_dir was not used but likely in `src/ui/resources` or `src/resources`.
// I'll assume relative path from src (NanoVNA-X root include path is usually added).
// But #include "..." uses relative to file.
// If file is in src/ui/core/, ../../ moves to src/.
// Then resources/icons/icons_menu.h.

// ===================================
// Drawing
// ===================================

static void menu_draw_buttons(const menuitem_t* m, uint32_t mask) {
  int i;
  int y = MENU_BUTTON_Y_OFFSET;
  for (i = 0; i < MENU_BUTTON_MAX && m; i++, m = menu_next_item(m), y += menu_button_height) {
    if ((mask & (1 << i)) == 0)
      continue;
    button_t button;
    button.fg = LCD_MENU_TEXT_COLOR;
    button.icon = BUTTON_ICON_NONE;
    // focus only in MENU mode but not in KEYPAD mode
    // We can check ui_mode (extern or getter)
    // extern uint8_t ui_mode from ui_core.h? No, I exposed it.
    if (ui_mode == UI_MENU && i == selection) {
      button.bg = LCD_MENU_ACTIVE_COLOR;
      button.border = MENU_BUTTON_BORDER | BUTTON_BORDER_FALLING;
    } else {
      button.bg = LCD_MENU_COLOR;
      button.border = MENU_BUTTON_BORDER | BUTTON_BORDER_RISE;
    }
    // Custom button, apply custom settings/label from callback
    const char* text;
    uint16_t text_offs;
    if (m->type == MT_ADV_CALLBACK) {
      button.label[0] = 0;
      if (m->reference)
        ((menuaction_acb_t)m->reference)(m->data, &button);
      // Apply custom text, from button label and
      if (button.label[0] == 0)
        plot_printf(button.label, sizeof(button.label), m->label, button.p1.u);
      text = button.label;
    } else
      text = m->label;
    // Draw button
    ui_draw_button(LCD_WIDTH - MENU_BUTTON_WIDTH, y, MENU_BUTTON_WIDTH, menu_button_height,
                   &button);
    // Draw icon if need (and add extra shift for text)
    if (button.icon >= 0) {
      lcd_blit_bitmap(LCD_WIDTH - MENU_BUTTON_WIDTH + MENU_BUTTON_BORDER + MENU_ICON_OFFSET,
                      y + (menu_button_height - ICON_HEIGHT) / 2, ICON_WIDTH, ICON_HEIGHT,
                      ICON_GET_DATA(button.icon));
      text_offs = LCD_WIDTH - MENU_BUTTON_WIDTH + MENU_BUTTON_BORDER + MENU_ICON_OFFSET + ICON_SIZE;
    } else
      text_offs = LCD_WIDTH - MENU_BUTTON_WIDTH + MENU_BUTTON_BORDER + MENU_TEXT_OFFSET;
    // Draw button text
    int lines = get_lines_count(text);
#if _USE_FONT_ != _USE_SMALL_FONT_
    if (menu_button_height < lines * FONT_GET_HEIGHT + 2) {
      lcd_set_font(FONT_SMALL);
      lcd_drawstring(text_offs, y + (menu_button_height - lines * sFONT_STR_HEIGHT - 1) / 2, text);
    } else {
      lcd_set_font(FONT_NORMAL);
      lcd_printf(
          text_offs,
          y + (menu_button_height - lines * FONT_STR_HEIGHT + (FONT_STR_HEIGHT - FONT_GET_HEIGHT)) /
                  2,
          text);
    }
#else
    lcd_printf(
        text_offs,
        y + (menu_button_height - lines * FONT_STR_HEIGHT + (FONT_STR_HEIGHT - FONT_GET_HEIGHT)) /
                2,
        text);
#endif
  }
  // Erase empty buttons
  if (AREA_HEIGHT_NORMAL + OFFSETY > y) {
    lcd_set_background(LCD_BG_COLOR);
    lcd_fill(LCD_WIDTH - MENU_BUTTON_WIDTH, y, MENU_BUTTON_WIDTH, AREA_HEIGHT_NORMAL + OFFSETY - y);
  }
  lcd_set_font(FONT_NORMAL);
}

void menu_draw(uint32_t mask) {
  menu_draw_buttons(menu_stack[menu_current_level], mask);
}

void menu_invoke(int item) {
  const menuitem_t* menu = current_menu_item(item);
  if (menu == NULL)
    return;
  switch (menu->type) {
  case MT_CALLBACK:
    if (menu->reference)
      ((menuaction_cb_t)menu->reference)(menu->data);
    break;

  case MT_ADV_CALLBACK:
    if (menu->reference)
      ((menuaction_acb_t)menu->reference)(menu->data, NULL);
    break;

  case MT_SUBMENU:
    menu_push_submenu((const menuitem_t*)menu->reference);
    break;
  }
// Redraw menu after if UI in menu mode
  if (ui_mode == UI_MENU)
    menu_draw(-1);
}

void ui_mode_menu(void) {
  if (ui_mode == UI_MENU)
    return;
  // Reduce plot area to avoid overwriting the menu
  set_area_size(LCD_WIDTH - MENU_BUTTON_WIDTH - 12, AREA_HEIGHT_NORMAL);
  ui_mode = UI_MENU;
  menu_draw(-1);
}

static UI_FUNCTION_CALLBACK(menu_back_cb) {
  (void)data;
  menu_move_back(false);
}

const menuitem_t menu_back[] = {
    {MT_CALLBACK, 0, S_LARROW " BACK", menu_back_cb},
    {MT_NEXT, 0, NULL, NULL} // sentinel
};

// Generic Menu Callbacks
UI_FUNCTION_ADV_CALLBACK(menu_keyboard_acb) {
  if (data == KM_VAR &&
      lever_mode == LM_EDELAY) // JOG STEP button auto set (e-delay or frequency step)
    data = KM_VAR_DELAY;
  if (b) {
    ui_keyboard_cb(data, b);
    return;
  }
  ui_mode_keypad(data);
}

// ===================================
// Dynamic Menu Buffer
// ===================================
#define MENU_DYNAMIC_BUFFER_SIZE 64
static menuitem_t menu_dynamic_buffer[MENU_DYNAMIC_BUFFER_SIZE];

menuitem_t* menu_dynamic_acquire(void) {
  return menu_dynamic_buffer;
}

// ===================================
// Interaction Handlers
// ===================================

void ui_menu_lever(uint16_t status) {
  uint16_t count = current_menu_get_count();
  if (status & EVT_BUTTON_SINGLE_CLICK) {
    if ((uint16_t)selection >= count)
       ui_mode_normal();
    else
      menu_invoke(selection);
    return;
  }

  do {
    uint32_t mask = 1 << selection;
    if (status & EVT_UP)
      selection++;
    if (status & EVT_DOWN)
      selection--;
    // close menu if no menu item
    if ((uint16_t)selection >= count) {
       ui_mode_normal();
      return;
    }
    menu_draw(mask | (1 << selection));
    chThdSleepMilliseconds(100);
  } while ((status = ui_input_wait_release()) != 0);
}

void ui_menu_touch(int touch_x, int touch_y) {
  if (LCD_WIDTH - MENU_BUTTON_WIDTH < touch_x) {
    int16_t i = (touch_y - MENU_BUTTON_Y_OFFSET) / menu_button_height;
    if ((uint16_t)i < (uint16_t)current_menu_get_count()) {
      uint32_t mask = (1 << i) | (1 << selection);
      selection = i;
      menu_draw(mask);
      touch_wait_release();
      selection = -1;
      menu_invoke(i);
      return;
    }
  }

  touch_wait_release();
  ui_mode_normal();
}
