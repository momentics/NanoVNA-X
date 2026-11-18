# F072 trigonometry divisor correction

Proposed release note: correct the F072 sine/cosine quadrant divisor to use a
16-bit integer. The previous conversion of 256 to an 8-bit integer was out of
range and caused undefined behavior in the lookup-table path used by electrical
delay phase correction. F303 already uses a 16-bit quarter-table divisor.

The firmware change is limited to the F072 quarter-table constant and its uses.
The table values, interpolation algorithm and F303 implementation are unchanged.
No RAM buffer or saved configuration format is added.

Two new host targets compile the production F072 and F303 implementations without
`NANOVNA_HOST_TEST`, which otherwise substitutes library sine/cosine for F072.
Each checks 131,185 angles, including table nodes, intermediate values, quadrant
neighbors, negative values and multiple turns. The tests compare sine/cosine
against a double-precision reference and check unit-circle norm and complex
rotation magnitude. Run them as part of `make test`.

The existing final table interval has different interpolation behavior on each
chip. The F072 test allows 5.2e-6 absolute component error, based on its linear
interpolation bound plus float rounding; F303 allows 1e-6. This patch does not
claim to improve interpolation accuracy or to validate unrelated math helpers.

Host checks validate the C calculation paths, not physical RF accuracy or
calibration. All 28 local host suites passed normally and with sanitizers,
including float-cast-overflow checking. All four firmware variants built.
The correction saves four flash bytes on F072; F303 flash and both chips' RAM
are unchanged. The F072 builds fit their flash/RAM limits.

## Recorded local validation (2026-11-17)

On NanoVNA-H hw 3.7_ZK (F072), the combined image exercised +100 ns and -100 ns
S11 electrical delay across 50 kHz-50 MHz, returning complete finite binary
scans. The original delay was restored. CH0 and CH1 were joined with a through
cable; S11 reflection was below the comparison threshold at every point. This
is an ARM execution smoke check, not a quantitative phase/magnitude accuracy
test. F303 has not been tested on hardware.

The combined image included display PR #17, overlong-command rejection and
USB write watchdog servicing. NanoVNA-Saver 0.7.3 passed repeated 101-point
sweeps, eight complete three-segment 303-point sweeps, S11/S21 display, Stop,
screen capture and disconnect/reconnect. Received-data validation was enabled
for the final reconnect/run. A remaining startup issue made Saver parse the
`NanoVNA-X Shell` greeting as the version, show v0.0.0 and use legacy commands.
An apparent disconnect in the initial unlogged session did not recur in logged
runs; its cause is unknown. This correction does not address that startup issue
or establish complete companion compatibility or calibrated RF accuracy.

Combined firmware SHA-256:
`9e69d02338ae4d579f54f7b2b4593dc968277a1ede42d2382e07881a29fbad39`.
It occupies 98,132 bytes of the 100,352-byte firmware region and uses 15,024
static RAM bytes. Firmware built from this PR alone includes only the math
correction; reproducing the combined hw 3.7_ZK test also requires PR #17 and
the command/USB fixes. Roll back with a known-working image for the same chip
and RF profile; configuration and calibration formats are unchanged.
