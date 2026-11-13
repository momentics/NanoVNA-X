"""Exercise production shell writes over the production buffer-copy loop.

Only the hardware queue waits and USB consumption are simulated. In particular,
the driver's timeout is renewed on each buffer acquisition, not per write call.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]


def function(path, signature):
    text = (root / path).read_text()
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t systime_t;
typedef int msg_t;
#define MS2ST(n) ((systime_t)(n))
#define MSG_OK 0
#define SERIAL_USB_TX_BUFFERS_SIZE 64
typedef struct {
  uint8_t storage[64], *ptr, *top;
  size_t bsize;
} io_buffers_queue_t;
typedef io_buffers_queue_t BaseAsynchronousChannel;
static BaseAsynchronousChannel queue;
static bool shell_output_failed, connected, locked;
static systime_t now, last_feed, max_feed_gap, max_call_time;
static unsigned feeds, waits, disconnect_at;
static bool stalled;
static uint8_t output[8192];
static size_t output_size;
static int WDGD1;
static BaseAsynchronousChannel *shell_current_channel(void) { return &queue; }
static bool shell_check_connect(void) { return connected; }
static systime_t chVTGetSystemTimeX(void) { return now; }
static void observe_gap(void) {
  systime_t gap = now - last_feed;
  if (gap > max_feed_gap) max_feed_gap = gap;
}
static void wdgReset(int *driver) {
  assert(driver == &WDGD1 && !locked);
  observe_gap(); last_feed = now; feeds++;
}
static void osalSysLock(void) { assert(!locked); locked = true; }
static void osalSysUnlock(void) { assert(locked); locked = false; }
static msg_t obqGetEmptyBufferTimeoutS(io_buffers_queue_t *q, systime_t timeout) {
  assert(locked);
  waits++;
  if (waits == disconnect_at) connected = false;
  now += stalled || !connected ? timeout : MS2ST(90);
  observe_gap();
  if (stalled || !connected) return -1;
  q->ptr = q->storage; q->top = q->storage + sizeof(q->storage);
  return MSG_OK;
}
static msg_t ibqGetFullBufferTimeoutS(io_buffers_queue_t *q, systime_t timeout) {
  (void)q; (void)timeout; assert(false); return -1;
}
static void obqPostFullBufferS(io_buffers_queue_t *q, size_t size) {
  assert(locked && size == sizeof(q->storage));
  assert(output_size + size <= sizeof(output));
  memcpy(output + output_size, q->storage, size); output_size += size;
  q->ptr = NULL;
}
static void ibqReleaseEmptyBufferS(io_buffers_queue_t *q) { (void)q; assert(false); }
''' + function('third_party/ChibiOS/os/hal/src/hal_buffers.c',
               'static size_t bqCopyTimeout(') + r'''
static size_t chnWriteTimeout(BaseAsynchronousChannel *q, const uint8_t *data,
                              size_t size, systime_t timeout) {
  systime_t start = now;
  size_t n = bqCopyTimeout(q, true, (uint8_t *)data, size, timeout);
  if ((systime_t)(now - start) > max_call_time) max_call_time = now - start;
  return n;
}
''' + function('src/sys/shell_service.c', 'static bool shell_io_write(') + r'''
static void reset(void) {
  memset(&queue, 0, sizeof(queue));
  queue.bsize = sizeof(queue.storage) + sizeof(size_t);
  shell_output_failed = stalled = locked = false; connected = true;
  now = last_feed = max_feed_gap = max_call_time = 0;
  feeds = waits = disconnect_at = 0; output_size = 0;
}
static void flush_partial(void) {
  if (queue.ptr != NULL) {
    size_t n = (size_t)(queue.ptr - queue.storage);
    assert(output_size + n <= sizeof(output));
    memcpy(output + output_size, queue.storage, n); output_size += n;
  }
}
int main(void) {
  uint8_t data[4096];
  for (size_t i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i ^ (i >> 8));
  reset();
  assert(shell_io_write(data, sizeof(data)));
  flush_partial();
  assert(output_size == sizeof(data) && memcmp(output, data, sizeof(data)) == 0);
  printf("Slow host: elapsed=%u ms, longest call=%u ms, watchdog gap=%u ms, feeds=%u\n",
         now, max_call_time, max_feed_gap, feeds);
  fflush(stdout);
  assert(now > MS2ST(5000));
  assert(max_call_time <= MS2ST(100) && max_feed_gap <= MS2ST(100) && feeds > 0);

  // A prior short write leaves only one byte in the current driver buffer.
  reset();
  assert(shell_io_write(data, 63));
  assert(shell_io_write(data + 63, sizeof(data) - 63));
  flush_partial();
  assert(output_size == sizeof(data) && memcmp(output, data, sizeof(data)) == 0);
  assert(max_call_time <= MS2ST(100) && max_feed_gap <= MS2ST(100));

  reset(); stalled = true;
  assert(!shell_io_write(data, sizeof(data)));
  assert(now == MS2ST(1000) && output_size == 0 && shell_output_failed);
  unsigned before = waits;
  assert(!shell_io_write(data, 1) && waits == before);

  reset(); disconnect_at = 3;
  assert(!shell_io_write(data, sizeof(data)));
  assert(output_size == 128 && memcmp(output, data, 128) == 0);
  assert(shell_output_failed && !connected);
  before = waits;
  assert(!shell_io_write(data, 1) && waits == before);
  puts("[PASS] slow USB progress, unaligned chunks, stall and disconnect");
}
'''
with tempfile.TemporaryDirectory(prefix='nanovna-usb-progress-') as directory:
    p = Path(directory)
    (p / 'test.c').write_text(source)
    subprocess.run(shlex.split(os.environ.get('HOST_CC', 'gcc')) + [
        '-std=c11', '-Wall', '-Wextra', '-Werror', str(p / 'test.c'),
        '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
