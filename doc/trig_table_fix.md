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

Host checks validate the C calculation paths, not ARM execution, physical RF
accuracy or calibration. Local builds must still fit the F072 flash/RAM limits.
No hardware or GitHub Actions validation is claimed for this local candidate.
