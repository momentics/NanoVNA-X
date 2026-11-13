# Console input hardening

Proposed release note: reject overlong console commands instead of executing a
truncated prefix. The existing 63-byte command-text limit is unchanged. After
overflow, discard the rest of the line, report `error: command too long`, and
return to the prompt. Backspace/Delete remain usable before overflow; after
overflow, start a new line. This applies to console input, not SD script loading.

Host tests cover the exact length boundary, one-byte and larger overflow,
CR/LF/CRLF recovery, editing, buffer guards and disconnect during discard.

For the F303-ZK bench check, use a host script or terminal that sends the intended
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
upstream version. Roll back by flashing the previously working F303-ZK image.
Configuration and calibration storage formats are unchanged.
