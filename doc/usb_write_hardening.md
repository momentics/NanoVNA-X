# USB write reliability: release note and bench checks

Proposed release note: service the watchdog during slowly progressing USB
transfers. Limit each driver write to one transmit buffer so the driver's
per-buffer timeout cannot hold the shell inside one write for seconds. Preserve
the existing one-second inactivity timeout and failure latch. No new RAM buffer
or protocol framing is introduced.

The host regression uses the production shell writer and driver copy loop with
simulated queue waits. It does not validate physical USB operation or throughput.

Before release, test F072 and F303 with the same host application:

- Capture a full screen and read binary scan data at normal speed. Check exact
  lengths, data boundaries and the next prompt; compare transfer time to baseline.
- Throttle reads so progress continues for longer than the watchdog interval.
  Confirm complete payloads without a reboot or missing bytes.
- Stop reading for more than one second. Resume reading, discard the incomplete
  response and issue a fresh command; confirm command processing recovers.
- Disconnect the cable during capture, reconnect and reopen the port. Confirm
  commands work and no old transfer is treated as a complete new response.
- Close/reopen the port and toggle DTR/RTS during traffic. Separately test host
  suspend/resume; this patch does not change connection/session handling.

Rollback: flash the previously working firmware for the same chip and RF profile.
This change does not alter configuration or calibration storage formats.

## Recorded local validation (2026-11-17)

NanoVNA-H hw 3.7_ZK (F072), Windows USB host: with the original writer, reading
64 bytes every 2 ms during capture disconnected the port after 1.883 and 1.884
seconds in two trials. The device reappeared and answered `version` without a
host reset command. This is consistent with watchdog reset; reset-cause flags
were not read. The candidate completed the same throttled capture in 6.144 s;
the combined candidate completed it in 6.138 s. Both returned all 153,600 bytes
and the correct prompt. Windows buffering means this read cadence describes
application-level stress, not individual USB packet timing.

Both candidates passed five normal captures, a 2,024-byte binary scan (101 rows,
both channels, correct endpoints and finite values), recovery after a 3-second
host-read stall, DTR/RTS toggles and close/reopen during capture. The baseline
writer also completed a slower 10 ms read-cadence capture, so this is not simply
a minimum-throughput limit. Physical cable removal and suspend/resume remain
untested.

The combined image included display PR #17, overlong-command rejection and the
F072 trig divisor correction. With CH0 connected to CH1 through a cable,
NanoVNA-Saver 0.7.3 passed repeated 101-point sweeps, eight complete 303-point
three-segment sweeps over 50 kHz-50 MHz, S11/S21 display, Stop, screen capture
and disconnect/reconnect. Received-data validation was enabled for the final
reconnect/run. A separate startup issue made Saver parse `NanoVNA-X Shell` as
the version, show v0.0.0 and use legacy commands. An apparent disconnect in the
initial unlogged session did not recur in logged runs; its cause is unknown.
This does not establish complete Saver compatibility, RF accuracy, long-soak
stability or F303 hardware operation.

Combined firmware SHA-256:
`9e69d02338ae4d579f54f7b2b4593dc968277a1ede42d2382e07881a29fbad39`.
It uses 98,132 flash bytes and 15,024 static RAM bytes. This PR's standalone
firmware contains only the USB fix; the hw 3.7_ZK display requires PR #17.
All 27 local host suites passed normally and with sanitizers, and all four
firmware variants built. This fix leaves F072 flash unchanged, adds 12 flash
bytes on F303 and leaves RAM unchanged on both chips.
