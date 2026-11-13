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
