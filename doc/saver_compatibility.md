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

This local branch contains only the startup/model fix. Hardware testing on
NanoVNA-H hw 3.7_ZK uses a separate combined image with PRs #17-#20. No firmware
or branch is to be published until reviewed. Rollback uses the previously tested
combined F072-ZK image. Configuration and calibration formats are unchanged.
