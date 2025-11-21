# Test Plan

The `tests/` tree hosts host-executable checks that guard the most critical helper
functions without requiring any STM32 hardware.

- `tests/unit/` contains focused suites that link against the production sources
  and validate behaviour with a regular POSIX toolchain.  Current suites cover:
  - `test_common.c`: CLI parsing helpers (`my_atof`, `parse_line`, `packbits`, …)
  - `test_vna_math.c`: LUT-driven trig/FFT helpers used by the DSP pipeline
  - `test_measurement_pipeline.c`: integration glue that proxies sweep requests
- `tests/stubs/` provides lightweight stand-ins for headers that normally come
  from ChibiOS/HAL so that host builds can compile firmware files.

## Running locally

```sh
make test
```

The `test` target builds every suite into `build/tests/*.out` and executes them.
Failures are reported with descriptive messages.

## Extending

1. Drop a new `tests/unit/test_*.c` file containing `main()` and register it in
   the `TEST_SUITES` variable inside the top-level `Makefile`.
2. Reuse existing stubs or add new ones (e.g. mock `ch.h`) under `tests/stubs/`.
3. Keep test comments verbose—CI logs must tell a future maintainer what a
   regression means without opening the sources.
