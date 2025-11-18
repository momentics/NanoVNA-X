# Platform board layout

The `src/platform/boards` directory provides the hardware-specific half of the
platform abstraction.  Each supported MCU family gets its own subfolder that
contains:

* `config/` – `board.[ch]`, `mcuconf.h`, linker scripts and any other files
  that the ChibiOS build expects via `board.mk`.
* `drivers/` – lightweight peripheral shims (`adc_v*`, `i2c_v*`, `gpio_v*`,
  etc.) that implement the routines declared in
  `include/platform/boards/stm32_peripherals.h`.  They are compiled by
  including the `.c` file from `src/platform/boards/stm32_peripherals.c`,
  ensuring a single, well-defined API surface is exposed to the rest of the
  firmware.
* Optional helper folders (for example, `openocd/` for the STM32F303 board)
  with debugging scripts and interface definitions.

Adding a new board means adding another directory next to `stm32f072/` and
`stm32f303/`, defining the ChibiOS configuration under `config/`, wiring the
low-level drivers under `drivers/`, and pointing `config/mcuconf.h` and the
top-level `Makefile` at the new paths.  Higher layers (measurement, UI, USB)
interact exclusively with the functions declared in the `include/platform`
headers, keeping the application logic independent of MCU- or board-specific
details.
