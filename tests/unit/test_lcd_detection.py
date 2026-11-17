"""Verify production F072 LCD selection, including the newer ST7789P3 panel."""
from pathlib import Path
import os, re, shlex, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
text = (root / 'src/driver/lcd.c').read_text()
start = text.index('static const uint8_t* get_lcd_init(void)')
end = text.index('{', start) + 1
depth = 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
ids = '\n'.join(re.findall(r'^#define ST7789\w+_ID[^\n]*', text, re.M))
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef enum { ili9341_type = 0, st7789v } lcd_type_t;
static lcd_type_t lcd_type;
static const uint8_t ili9341_init_seq[] = {0};
static const uint8_t ST7789V_init_seq[] = {1};
static uint32_t raw_id;
#define LCD_RDDID 4
static uint32_t lcd_send_register(uint8_t cmd, uint8_t size, const uint8_t *data) {
    assert(cmd == LCD_RDDID && size == 0 && data == NULL);
    return raw_id;
}
''' + ids + '\n' + text[start:end] + r'''
static void check(uint32_t raw, lcd_type_t expected) {
    lcd_type = ili9341_type; // Power-on initial state.
    raw_id = raw;
    const uint8_t *sequence = get_lcd_init();
    assert(lcd_type == expected);
    assert(sequence == (expected == st7789v ? ST7789V_init_seq : ili9341_init_seq));
}
int main(void) {
    check(0x40c0d9ff, st7789v); // ST7789P3, observed on NanoVNA-H ZK.
    check(0x42c2a97f, st7789v); // Existing ST7789V.
    check(0, ili9341_type);    // Existing fallback.
    check(0xffffffff, ili9341_type);
    check((0x8181b2U << 7) | 0x7f, ili9341_type);
    check((0x8181b4U << 7) | 0x7f, ili9341_type);
    puts("[PASS] LCD IDs select existing init sequences; P3, V and fallback checked");
}
'''
with tempfile.TemporaryDirectory(prefix='nanovna-lcd-test-') as d:
    p = Path(d)
    (p/'test.c').write_text(source)
    subprocess.run(shlex.split(os.environ.get('HOST_CC','gcc')) + [
        '-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
