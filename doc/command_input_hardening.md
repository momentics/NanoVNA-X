# Console input hardening

Proposed release note: reject overlong console commands instead of executing a
truncated prefix. The existing 63-byte command-text limit is unchanged. After
overflow, discard the rest of the line, report `error: command too long`, and
return to the prompt. Backspace/Delete remain usable before overflow; after
overflow, start a new line. This applies to console input, not SD script loading.

Host tests cover the exact length boundary, one-byte and larger overflow,
CR/LF/CRLF recovery, editing, buffer guards and disconnect during discard.

For a bench check, use a host script or terminal that sends the intended
bytes without local editing:

1. Send `version` followed by 56 spaces and a newline (63 text bytes). Expect
   the normal version response and prompt.
2. Send `version` followed by 57 spaces and a newline (64 text bytes). Expect
   the overflow error and prompt, with no version response.
3. Repeat using CR, LF and CRLF, immediately followed by `version` and a newline.
   Expect exactly one rejection, then a normal response to the fresh command.
4. Send an overlong line followed by backspace/Delete and more text. Confirm
   it stays rejected until the line ending, and the next command works.
5. Disconnect during an overlong unfinished line, reconnect and reopen the port.
   Confirm a fresh `version` command works. Physical reconnection is not covered
   by the host simulation.

This candidate is independent of the USB watchdog branch. Identify firmware by
its candidate directory and SHA-256 checksum; its displayed version remains the
upstream version. Roll back by flashing the previously working image for the
same chip and RF profile.
Configuration and calibration storage formats are unchanged.

## Recorded local validation (2026-11-17)

NanoVNA-H hw 3.7_ZK (F072), with ST7789P3 detection from PR #17: the baseline
executed truncated prefixes of oversized `version` commands. This fix, both
alone with the display fix and in the combined image, passed 14 hardware cases:
63/64/256/1024-byte lines, CR/LF/CRLF, editing, sticky overflow, next-command
recovery and DTR session loss during an unfinished line. Normal commands and
screen capture worked afterward. Physical cable removal was not tested.

The combined image also included USB write watchdog servicing and the F072 trig
divisor correction. With a through cable between CH0 and CH1, NanoVNA-Saver
0.7.3 completed repeated 101-point sweeps and eight complete three-segment
303-point sweeps over 50 kHz-50 MHz. S11/S21 display, Stop, screen capture and
disconnect/reconnect passed. Saver's received-data validation was enabled for
the final reconnect/run. A remaining startup issue caused Saver to parse the
`NanoVNA-X Shell` greeting as the version, show v0.0.0 and use its legacy
commands; this change does not fix that issue. An apparent disconnect in the
initial unlogged session was not reproduced in the logged runs; its cause is
unknown. No F303 hardware, suspend/resume, long soak or RF accuracy validation
is claimed.

Combined firmware SHA-256:
`9e69d02338ae4d579f54f7b2b4593dc968277a1ede42d2382e07881a29fbad39`.
It occupies 98,132 bytes of the 100,352-byte F072 firmware region, with 15,024
bytes static RAM. Firmware built from this PR alone contains only the command
fix; reproducing the hw 3.7_ZK combined test also requires the display, USB and
math changes. Local host tests and all four chip/RF-profile builds passed;
this command fix adds 60 flash bytes on F072 and 68 on F303, with no RAM change.
