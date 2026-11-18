# NanoVNA-Saver startup compatibility

Proposed release note: keep the console silent when a session opens and send
the prompt after a completed input line. This prevents a delayed startup banner
or prompt from being mistaken for the reply to the first command. Interactive
terminal users can press Enter for a prompt. Command echo and response framing
remain unchanged. No host-specific delays or USB queue resets are added.

Report the F072 board as `NanoVNA-H` so NanoVNA-Saver selects its H model.
F303 continues to report `NanoVNA-H 4`. The RF profile and firmware version
suffix are unchanged; this is model identification, not automatic RF-chip
detection or a claim of high-frequency measurement accuracy.

NanoVNA-Saver 0.7.3 selects scan-mask acquisition for versions >= 0.7.1.
Validate that connection reports the actual version and `Scan mask command`,
then check repeated 101-point and segmented 303-point sweeps, Stop, screenshot,
and reconnect. Log the outgoing `scan ... 0b001` and `scan ... 0b110` commands.
Also exercise immediate `version` after opening, delayed startup, blank-line
synchronization, and repeated close/reopen. Test F072 and F303 builds separately.

This branch contains the startup/model fix against upstream 0.9.105, which
already includes #17, #18 and #20. The branch does not include #19. Earlier
hardware testing used a combined 0.9.104-ZK image with all four PRs.
Roll back with a known-working image for the same chip and RF profile. The
configuration and calibration formats are unchanged.

## Local results, 2026-11-17

The isolated fix passed all 27 host suites normally and with ASan/UBSan and
float-cast-overflow checks. The combined image passed all 31 suites under the
same checks. Both sources built all four F072/F303 STANDARD/ZK variants using
the pinned Arm GNU 14.2.1 toolchain and LTO. The isolated change saves 32 flash
bytes on F072 and 20 on F303; RAM is unchanged on both chips. F303 validation
is limited to host checks and compilation, not physical hardware.

On NanoVNA-H hw 3.7_ZK, the previous combined firmware reproduced the startup
race in six of ten timed first-command trials: its banner and initial prompt
arrived before the real version reply. The candidate passed all ten trials
(two each at 0, 50, 150, 400 and 1200 ms after opening the port). The test clears
the host input buffer before sending `version`, demonstrating why a drain alone
does not fix output that arrives afterward.

NanoVNA-Saver.exe 0.7.3 on Windows identified `NanoVNA-H v0.9.104-ZK` on both
connections and listed `Scan mask command`. Its diagnostic log confirms actual
`scan ... 0b001` frequency requests and `scan ... 0b110` paired-channel requests.
With the CH0-to-CH1 through cable, it completed 69 101-point sweeps and nine full
303-point three-segment cycles over 50 kHz-50 MHz. Stop, screenshot capture and
disconnect/reconnect passed, with received-data validation enabled. No version
parsing errors or ERROR/CRITICAL log entries were recorded. Saver was left
connected and idle in single-sweep mode with 101 points.

The flashed combined source is `f8183239326311d5773998fceb9e6ed620e438c2`
(prior combined source `6240922` plus compatibility source `6d70bd0`). Firmware
SHA-256: `600847da549197466001a62ce660f634d0afcd46f6b874e5af33e56c24c94274`.
STM32CubeProgrammer checked the F072 identity and verified the programmed bytes.
The combined F072-ZK image uses 98,100 of 100,352 firmware bytes and 15,024 bytes
static RAM. This is compatibility testing, not calibrated RF accuracy,
high-frequency performance, long-duration soak or USB suspend/resume validation.
See the PR for GitHub Actions results and current per-variant firmware
downloads.

Saver's H model configures a 1.5 GHz upper sweep limit, instead of the generic
model's 300 MHz limit. The model selection was verified; the sweeps recorded
above cover only 50 kHz-50 MHz, not operation or accuracy near 1.5 GHz.

## Standalone retest on upstream 0.9.105 (2026-11-18)

The rebased source `5bd17ef` against `3f22b00` passed all 30 host suites,
including the console startup and upstream LCD regressions, normally and
with sanitizers. All four firmware variants built. The F072-ZK image uses
98,100 firmware bytes and 15,024 bytes of static RAM.

On the same NanoVNA-H hw 3.7_ZK, this standalone image passed ten
version-first serial opens with delays from 0 to 1,200 ms. Each returned the
exact echo, version and single prompt in order, then a valid blank-line
prompt and the H model in `info`. Three further opens sent a blank line
first and received the expected prompt followed by a valid version exchange.

NanoVNA-Saver 0.7.3 connected, completed a 101-point scan, released the port
on application close, and reconnected after relaunch. Both launches logged
`NanoVNA-H v0.9.105-ZK`, used masks `0b001` and `0b110`, and completed
50 kHz-50 MHz sweeps without logged errors. This retest used an application
restart for the Saver reconnect. The recorded runs above used the older
combined firmware.

Verified F072-ZK firmware SHA-256:
`7d025f65167f91aab854cebb74ccac64e328e58b3ca702d9cf88288f0fd998ab`.
