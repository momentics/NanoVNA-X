# Test Plan

The `tests/` tree hosts host-executable checks that guard the most critical helper
functions without requiring any STM32 hardware.

- `tests/unit/` contains focused suites that link against the production sources
  and validate behaviour with a regular POSIX toolchain.  Current suites cover:
  - `test_common.c`: CLI parsing helpers (`my_atof`, `parse_line`, `packbits`, …)
  - `test_vna_math.c`: LUT-driven trig/FFT helpers used by the DSP pipeline
  - `test_measurement_pipeline.c`: integration glue that proxies sweep requests
  - `test_dsp_backend.c`: scalar DSP accumulation path that runs when SIMD is disabled
  - `test_legacy_measure.c`: RF legacy analytics (quadratic solver, cursor search, regression)
  - `test_event_bus.c`: synchronous/asynchronous event bus dispatch with mailbox recycling
  - `test_scheduler.c`: cooperative task scheduler slot allocation, failure paths, and stop logic
  - `test_measurement_engine.c`: RF engine state machine, event publication, and sweep orchestration
  - `test_shell_service.c`: CLI parser/buffer handling plus deferred command queue + event bus glue
  - `test_display_presenter.c`: presenter wrappers that forward drawing calls to the active API
- `tests/stubs/` provides lightweight stand-ins for headers that normally come
  from ChibiOS/HAL so that host builds can compile firmware files.

## Running locally

```sh
make test
```

The `test` target builds every suite into `build/tests/*.out` and executes them.
Failures are reported with descriptive messages.

Production-function Python harnesses contributed by @jebcampbellhm in
[PR #11](https://github.com/momentics/NanoVNA-X/pull/11) and
[PR #13](https://github.com/momentics/NanoVNA-X/pull/13) also run under `make test`.
They compile extracted production functions rather than duplicate their logic:
startup rollover/ADC readiness, nested IRQ scheduling, CDC line-state changes,
exact SD I/O, transactional CAL import, Touchstone parsing, calibration/math,
DMA flag ownership, flash boundaries, and I2C fault recovery. The timing harness
is adapted to our suspend/resume capture implementation. Override `HOST_PYTHON`
or `HOST_CC` when the host tools have different names.

## Extending

1. Drop a new `tests/unit/test_*.c` file containing `main()` and register it in
   the `TEST_SUITES` variable inside the top-level `Makefile`.
2. Reuse existing stubs or add new ones (e.g. mock `ch.h`) under `tests/stubs/`.
3. Keep test comments verbose—CI logs must tell a future maintainer what a
   regression means without opening the sources.
