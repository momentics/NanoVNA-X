"""Exercise the production CDC control hook without resetting active queues."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
text = (root / 'src/driver/usbcfg.c').read_text()
start = text.index('bool custom_sduRequestsHook(')
end = text.index('{', start) + 1
depth = 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1

source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define USB_RTYPE_TYPE_MASK 0x60
#define USB_RTYPE_TYPE_CLASS 0x20
#define CDC_GET_LINE_CODING 0x21
#define CDC_SET_LINE_CODING 0x20
#define CDC_SET_CONTROL_LINE_STATE 0x22
typedef struct { struct { unsigned bmRequestType, bRequest, wValue; } setup; } USBDriver;
static uint8_t linecoding[7];
static unsigned transfers, wakes, queue_resets;
static bool locked, connected;
static void chSysLockFromISR(void) { assert(!locked); locked=true; }
static void chSysUnlockFromISR(void) { assert(locked); locked=false; }
static void shell_update_vcp_connection_state(bool c) { assert(locked); connected=c; }
static void shell_wake_all_waiting_threadsI(void) { assert(locked); wakes++; }
static void usbSetupTransfer(USBDriver *u,void *p,size_t n,void *cb) {
  (void)u; (void)p; (void)n; (void)cb; assert(!locked); transfers++;
}
// Queue resets invalidate buffer ownership while USB endpoints remain active.
#define sduConfigureHookI(p) (++queue_resets)
#define sduDisconnectI(p) (++queue_resets)
''' + text[start:end] + r'''
static void control(USBDriver *u,unsigned value) {
  u->setup.bRequest=CDC_SET_CONTROL_LINE_STATE; u->setup.wValue=value;
  unsigned before=transfers;
  assert(custom_sduRequestsHook(u));
  assert(transfers==before+1 && !locked && queue_resets==0);
  assert(connected==((value&1)!=0));
}
int main(void) {
  USBDriver u={.setup={USB_RTYPE_TYPE_CLASS,0,0}};
  control(&u,1); // Open while configured endpoint already owns an RX buffer.
  control(&u,1); // Repeated DTR request must leave active transfer intact.
  control(&u,3); // RTS toggle, DTR still asserted.
  assert(wakes==0);
  control(&u,0); assert(wakes==1);
  control(&u,1); assert(wakes==1); // Reopen, still no bulk queue resets.
  for(unsigned i=0;i<100;i++) { control(&u,0); control(&u,1); }
  u.setup.bRequest=CDC_GET_LINE_CODING; assert(custom_sduRequestsHook(&u));
  u.setup.bRequest=CDC_SET_LINE_CODING; assert(custom_sduRequestsHook(&u));
  u.setup.bRequest=0xff; assert(!custom_sduRequestsHook(&u));
  u.setup.bmRequestType=0; assert(!custom_sduRequestsHook(&u));
  puts("[PASS] CDC open/reopen/RTS changes preserve queues and lock RTOS calls");
}
'''
with tempfile.TemporaryDirectory(prefix='nanovna-usb-') as d:
    p = Path(d)
    (p / 'test.c').write_text(source)
    subprocess.run(shlex.split(os.environ.get('HOST_CC', 'gcc')) + [
        '-std=c11', '-Wall', '-Wextra', '-Werror', str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
