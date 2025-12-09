#include "nanovna.h"
#include "interfaces/cli/shell_service.h"

/* The prototype shows it is a naked function - in effect this is just an
assembly function. */
void HardFault_Handler(void);

typedef struct {
  uint32_t r4;
  uint32_t r5;
  uint32_t r6;
  uint32_t r7;
  uint32_t r8;
  uint32_t r9;
  uint32_t r10;
  uint32_t r11;
} hard_fault_extra_registers_t;

__attribute__((noreturn)) void
hard_fault_handler_c(uint32_t* sp, const hard_fault_extra_registers_t* extra, uint32_t exc_return);

__attribute__((naked, used)) void HardFault_Handler(void) {
  __asm volatile("mov r2, lr\n"
                 "movs r3, #4\n"
                 "tst r3, r2\n"
                 "beq 1f\n"
                 "mrs r0, psp\n"
                 "b 2f\n"
                 "1:\n"
                 "mrs r0, msp\n"
                 "2:\n"
                 "sub sp, #32\n"
                 "mov r1, sp\n"
                 "stmia r1!, {r4-r7}\n"
                 "mov r3, r8\n"
                 "str r3, [r1, #0]\n"
                 "mov r3, r9\n"
                 "str r3, [r1, #4]\n"
                 "mov r3, r10\n"
                 "str r3, [r1, #8]\n"
                 "mov r3, r11\n"
                 "str r3, [r1, #12]\n"
                 "mov r1, sp\n"
                 "bl hard_fault_handler_c\n"
                 "add sp, #32\n"
                 "b .\n");
}

void hard_fault_handler_init(void) {
  // Dummy function to force linker to keep this object file
}

__attribute__((used)) void hard_fault_handler_c(uint32_t* sp, const hard_fault_extra_registers_t* extra,
                          uint32_t exc_return) {
#ifdef ENABLE_HARD_FAULT_HANDLER_DEBUG
  uint32_t r0 = sp[0];
  uint32_t r1 = sp[1];
  uint32_t r2 = sp[2];
  uint32_t r3 = sp[3];
  uint32_t r12 = sp[4];
  uint32_t lr = sp[5];
  uint32_t pc = sp[6];
  uint32_t psr = sp[7];
  int y = 0;
  int x = 20;
  lcd_set_colors(LCD_FG_COLOR, LCD_BG_COLOR);
  lcd_printf(x, y += FONT_STR_HEIGHT, "SP  0x%08x", (uint32_t)sp);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R0  0x%08x", r0);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R1  0x%08x", r1);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R2  0x%08x", r2);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R3  0x%08x", r3);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R4  0x%08x", extra->r4);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R5  0x%08x", extra->r5);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R6  0x%08x", extra->r6);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R7  0x%08x", extra->r7);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R8  0x%08x", extra->r8);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R9  0x%08x", extra->r9);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R10 0x%08x", extra->r10);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R11 0x%08x", extra->r11);
  lcd_printf(x, y += FONT_STR_HEIGHT, "R12 0x%08x", r12);
  lcd_printf(x, y += FONT_STR_HEIGHT, "LR  0x%08x", lr);
  lcd_printf(x, y += FONT_STR_HEIGHT, "PC  0x%08x", pc);
  lcd_printf(x, y += FONT_STR_HEIGHT, "PSR 0x%08x", psr);
  lcd_printf(x, y += FONT_STR_HEIGHT, "EXC 0x%08x", exc_return);

  shell_printf("===================================" VNA_SHELL_NEWLINE_STR);
#else
  (void)sp;
  (void)extra;
  (void)exc_return;
#endif
  while (true) {
  }
}
