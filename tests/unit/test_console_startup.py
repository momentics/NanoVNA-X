"""Compile the production console loops; a late open must not terminate a reply.

Exercise both runtime loop variants and both SD configurations. The mocked
reader checks silence before the first line and simulates a command already
waiting when the firmware observes the connection, plus blank-line sync and
disconnect. USB timing itself is checked on hardware.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
text = (root / 'src/runtime/runtime_entry.c').read_text()


def block(start):
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


session = text[text.index('static void usb_command_session_started(void);'):
               text.index('// Shell frequency printf format')]
thread = block(text.index('THD_FUNCTION(myshellThread, p)'))
start = text.index('    do {', text.index('int runtime_main(void)'))
main_loop = block(start) + ' while (shell_check_connect());'
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
typedef int FATFS;
typedef int FIL;
#define THD_FUNCTION(n,p) void n(void *p)
#define VNA_SHELL_PROMPT_STR "ch> "
#define VNA_SHELL_NEWLINE_STR "\r\n"
#define VNA_SHELL_MAX_LENGTH 64
static char shell_line[64], output[256];
static const char *input;
static int reads, executes;
static jmp_buf finished;
static void chRegSetThreadName(const char *s) { (void)s; }
static int shell_printf(const char *fmt, ...) {
  va_list ap; va_start(ap,fmt);
  int n = vsnprintf(output+strlen(output),sizeof(output)-strlen(output),fmt,ap);
  va_end(ap); return n;
}
static int vna_shell_read_line(char *line, int max) {
  assert(max == 64);
  if (reads++) longjmp(finished,1);
  assert(output[0] == 0); // No startup banner/prompt before the queued command.
  if (!input) return 0;   // Disconnect, not a completed line.
  strcpy(line,input);
  shell_printf("%s\r\n",input); // Existing line reader echo.
  return 1;
}
static void vna_shell_execute_line(char *line) {
  if (!*line) return;
  assert(!strcmp(line,"version"));
  executes++;
  shell_printf("0.9.104-ZK\r\n");
}
static void chThdSleepMilliseconds(int ms) { assert(ms > 0); longjmp(finished,1); }
static bool shell_check_connect(void) { return false; }
'''
suffix = r'''
static void check(void (*loop)(void *), const char *request, const char *reply) {
  input=request; output[0]=0; reads=executes=0;
  usb_command_session_started();
  if (!setjmp(finished)) loop(NULL);
  usb_command_session_stopped();
  assert(!strcmp(output,reply));
  assert(executes == (request && *request ? 1 : 0));
}
int main(void) {
  for (int i=0;i<3;i++) {
    void (*loop)(void *)=i%2 ? myshellThread : main_console;
    check(loop,"version","version\r\n0.9.104-ZK\r\nch> ");
    check(loop,"","\r\nch> ");
    check(loop,NULL,"");
  }
  puts("[PASS] delayed first command, blank-line sync, disconnect and reopen");
}
'''
with tempfile.TemporaryDirectory(prefix='nanovna-console-startup-') as d:
    p = Path(d)
    (p/'test.c').write_text(prefix + session + thread +
                          '\nstatic void main_console(void *p) { (void)p;\n' +
                          main_loop + '\n}\n' + suffix)
    for sd in (False, True):
        args = ['-D__USE_SD_CARD__'] if sd else []
        subprocess.run(shlex.split(os.environ.get('HOST_CC', 'gcc')) +
                       ['-std=c11','-Wall','-Wextra','-Werror'] + args +
                       [str(p/'test.c'),'-o',str(p/'test')], check=True)
        subprocess.run([str(p/'test')], check=True)

# Match Saver's ordered model selection; do not turn H4 into an H.
for chip, expected in [('STM32F072','H'), ('STM32F303','H4')]:
    board = (root/'boards'/chip/'board.h').read_text()
    name = re.search(r'^#define BOARD_NAME\s+"([^"]+)"', board, re.M)[1]
    model = next((model for token, model in [('NanoVNA-H 4','H4'),
                 ('NanoVNA-H','H'),('NanoVNA','generic')] if token in name), None)
    assert model == expected, (chip, name, model)
print('[PASS] F072 identifies as H; F303 remains H4')
