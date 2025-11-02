<p align="center">
  <a href="https://github.com/momentics/NanoVNA-X/actions/workflows/firmware.yml"><img src="https://github.com/momentics/NanoVNA-X/actions/workflows/firmware.yml/badge.svg?branch=main" alt="Firmware CI status"></a>
  <a href="https://github.com/momentics/NanoVNA-X/actions/workflows/release-artifacts.yml"><img src="https://github.com/momentics/NanoVNA-X/actions/workflows/release-artifacts.yml/badge.svg" alt="Release workflow status"></a>
</p>

NanoVNA-X: Enhanced Firmware for NanoVNA H/H4
==========================================================
[Русская версия](README_RU.md)
[![Release](https://img.shields.io/github/v/release/momentics/NanoVNA-X)](https://github.com/momentics/NanoVNA-X/releases)
[![Maintenance](https://img.shields.io/maintenance/yes/2025)]()


<div align="center">
<img src="/doc/NanoVNA.jpg" width="280px">
</div>

# About

**NanoVNA-H** and **NanoVNA-H4** are very tiny handheld Vector Network Analyzers (VNA).
They are standalone portable devices with LCD and battery.

NanoVNA-X is maintained by **@momentics** (https://github.com/momentics/) and optimised for the
memory-constrained STM32F072 and STM32F303 platforms. The codebase is a continuation of
the outstanding work by [@DiSlord](https://github.com/DiSlord/) and is ultimately derived
from the original firmware created by [@edy555](https://github.com/edy555). The firmware
continues to be distributed under the terms of the GNU GPL so that the original authors
retain their rights to their work.

This repository contains the source code of the improved NanoVNA-H and NanoVNA-H4 firmware
used in the NanoVNA-X project. The documentation describes the build and flash process on a
macOS or a Linux (Debian or Ubuntu) system, other Linux (or even BSD) systems may behave
similar.

## Acknowledgment and Disclaimer

- Profound thanks to [@DiSlord](https://github.com/DiSlord/) for the original firmware sources and the foundations that significantly inspired and enabled the start of this new firmware project.
- This firmware evolves rapidly and, with each daily change, diverges further from the original implementation.
- The original codebase was exceptionally feature‑rich; to make ongoing development feasible, the SD subsystem has been temporarily removed to reduce complexity and unblock core refactoring.
- The SD subsystem will return once the key features are implemented as originally envisioned and the architecture is ready to support it cleanly.

## Improvements​
* **Event bus and task helpers.** The firmware exposes a lightweight event bus with typed topics so UI code, measurement logic, storage handlers, and input adapters can exchange notifications without direct dependencies. A thin scheduler helper wraps ChibiOS thread creation/termination but otherwise relies on standard RTOS primitives.
* **Measurement pipeline facade.** A dedicated pipeline object bridges platform drivers and the sweep service by handing off execution requests and exposing the active sweep mask, keeping higher layers agnostic of board-specific driver tables.
* **Sweep engine tracking.** The sweep service maintains generation counters, UI progress indicators, and LED feedback while orchestrating measurement loops and calibration state.
* **Persistent configuration service.** Configuration and calibration saves are validated with rolling checksums, cached per-slot integrity flags, and a single API surface that hides flash programming details from application code.
* **Platform driver registry.** Hardware bring-up flows through a board registry that selects the correct driver table for each target and runs optional pre-initialisation hooks, reducing conditional logic scattered across the firmware.
* **USB console handshake compatibility.** The USB shell now emits its `ch> ` prompt before the banner so host utilities such as NanoVNA-Saver detect the device without manual retries.

### PLL transient stabilization​
PLL transients are stabilized by optimizing the synthesizer programming sequence and precomputing capture parameters: staged delays, lock‑status gating, and reference frequency caching minimize retuning overhead, reducing overshoot and drift at sweep start and during rapid retunes.​
Loop‑filter parameters and output enable order were further refined to cut transient amplitude and accelerate phase settling across operating sub‑bands; this improves measurement repeatability and reduces reliance on additional DSP smoothing.
The Si5351 driver now tracks pending band changes, triggers PLL resets only when necessary, and requests extra settling cycles so the sweep loop automatically discards unstable conversions before logging final data; fractional divider approximations were tightened to keep output frequencies aligned with the cached PLL plans.

## Architecture Overview

NanoVNA-X embraces a layered architecture that separates bootstrapping, platform
integration, measurement processing and the user interface so the firmware remains
maintainable while fitting into the tight flash and RAM budgets of the STM32F072 and
STM32F303 families.

* **Boot and application runtime.** The firmware starts in `src/core/main.c`, which hands off
  to `app_main()` so that all higher level wiring lives in `src/app/application.c`. That
  entry point initialises ChibiOS/RT, configures the USB console and synthesiser drivers,
  brings up the measurement pipeline, and spins a dedicated sweep thread that coordinates
  measurements, UI updates and shell commands.
* **Platform abstraction.** `src/platform/platform_hal.c` invokes the registered board driver
  table from `src/platform/boards/` so each supported target can publish a `PlatformDrivers`
  structure with its initialisation hooks and peripheral descriptors. This keeps the core
  application agnostic of whether it is running on the NanoVNA-H (STM32F072) or NanoVNA-H4
  (STM32F303) hardware variants.
* **Services layer.** Shared infrastructure lives in `src/services/`. The event bus provides
  a publish/subscribe mechanism with a fixed topic list and optional mailbox-backed delivery
  so code running in interrupt context can queue work for later dispatch. The scheduler helper
  wraps ChibiOS thread creation and termination, while the configuration service persists user
  settings and calibration slots in MCU flash with checksum protection.
* **Measurement pipeline and DSP.** `src/measurement/pipeline.c` provides a slim façade that
  bridges platform drivers and the measurement routines inside `app/application.c`. It forwards
  sweep execution to the application layer and reports the current channel mask. Numerical
  helpers and calibration maths reside in `src/dsp/`, keeping compute-heavy code isolated from
  hardware access.
* **Drivers and middleware.** Low-level device interactions are implemented in `src/drivers/`
  for the LCD, Si5351 synthesiser, TLV320 codec and USB front-end, while `src/middleware/`
  houses small integration shims such as the `chprintf` binding for ChibiOS streams. ChibiOS
  itself is vendored under `third_party/` and configured through the headers in `config/`
  and top-level `chconf.h`/`halconf.h`.
* **User interface layer.** The sweep thread initialises the UI toolkit (`src/ui/`), processes
  hardware inputs, refreshes plotting primitives and marks screen regions for redraw. Fonts
  and icon bitmaps that back the rendering code are stored in `src/resources/`.

Complementary headers live in `include/`, while board support files, linker scripts and
startup code reside in `boards/`. This structure lets the same measurement and UI engines run
across both memory profiles with only targeted platform overrides.

## Event bus and scheduler

The event bus (`services/event_bus.[ch]`) is a small publish/subscribe helper that avoids
dynamic allocation. Call `event_bus_init()` with a static subscription array, optional
mailbox storage (`msg_t` buffer plus queue length), and optional queue nodes that back the
mailbox entries. When the mailbox buffers are provided, `event_bus_publish()` posts messages
through the mailbox so a dedicated worker can call `event_bus_dispatch()` and fan them out to
listeners. Without a mailbox the bus falls back to immediate, synchronous dispatch. Interrupt
handlers should invoke `event_bus_publish_from_isr()`, which acquires queue slots in a
lock-aware fashion. The predefined topics (`EVENT_SWEEP_STARTED`, `EVENT_SWEEP_COMPLETED`,
`EVENT_TOUCH_INPUT`, `EVENT_STORAGE_UPDATED`, `EVENT_CONFIGURATION_CHANGED`, and
`EVENT_SHELL_COMMAND_PENDING`) cover the current coordination needs; adding new topics
requires extending the `event_bus_topic_t` enum.

The scheduler helper (`services/scheduler.[ch]`) keeps a fixed pool of four slots that wrap
`chThdCreateStatic()`/`chThdTerminate()`. `scheduler_start()` returns a handle containing the
assigned slot so callers can later stop the worker through `scheduler_stop()`. It does not
implement time slicing or cooperative yielding; task priorities and timing remain under the
control of ChibiOS.

## Hardware platform and used chips
The primary target MCUs are the STM32F072xB and STM32F303 (NanoVNA-H/H4); the F072 board employs an 8 MHz crystal plus USB, SPI, I²C, and I²S lines for the display, codec, and touch sensor, as described in board.h.

The Si5351A clock generator (or a compatible part) drives the RF synthesizers and also produces reference frequencies for the audio ADC; its driver implements frequency caching, power management, and PLL initialization over I²C.

The TLV320AIC3204 acts as a dual-channel audio ADC/DAC (I²S), with PLL configuration and input routing; the firmware controls it via I²C and streams the working data over SPI/I²S.

Display controllers ILI9341/ST7789 (320×240) and ST7796S (480×320) are selected per board; DMA is supported, along with brightness control (for the H4), inversion, and text shadows to improve readability.

Additional features include an RTC, persistent calibration/configuration in internal flash, USB UID, remote control, and a measurement module (LC matching, cable analysis, and resonance analysis).

## Building the firmware

NanoVNA-X uses a standard GNU Make workflow and the Arm GNU GCC toolchain.
See [`doc/building.md`](doc/building.md) for a fully detailed guide; the
Russian translation is available in [`doc/building_ru.md`](doc/building_ru.md).
summary below outlines the essential steps for a clean build.

### 1. Install the cross-compiler

Recent versions of `gcc-arm-none-eabi` are supported; there is no need to rely
on archived releases.

#### macOS (Homebrew)

```
brew install gcc-arm-none-eabi dfu-util
```

#### Ubuntu / Debian

```
sudo apt update
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi dfu-util
```

After installation confirm that the compiler is available:

```
arm-none-eabi-gcc --version
```

### 2. Fetch the source code

Clone the repository:

```
git clone https://github.com/momentics/NanoVNA-X.git
cd NanoVNA-X
```

Updating to the latest revision later only requires `git pull`.

### 3. Build the desired firmware profile

Set the `TARGET` environment variable to select the board and invoke `make`.
Use `make clean` when switching between targets to avoid mixing artefacts.

```
export TARGET=F072   # NanoVNA-H (default)
make clean
make -j$(nproc)
```

For the NanoVNA-H4 firmware build, set `TARGET=F303`. Build outputs are written
to `build/` (`H.bin`/`H4.bin`, `.elf`, `.hex`). See [doc/building.md](doc/building.md)
for a breakdown of which artefact to use with DFU, SWD, or debugging tools.

### 4. Flash the firmware

Place the device into DFU mode using one of the following methods:

* Open the device and jumper `BOOT0` pin to `Vdd` when powering the device.
* Select menu Config → DFU (requires recent firmware).
* Press the jog switch on your H4 while powering the device.

Then flash the binary generated in the previous step:

```
export TARGET=F072
make flash
```

`make flash` uses `dfu-util` under the hood and will upload the matching
`build/H.bin` or `build/H4.bin` artefact for the selected target. `dfu-util`
prints status diagnostics such as a transient "firmware is corrupt" warning
while switching the device out of DFU mode; the build script clears the status
automatically and the message can be ignored.

## Companion Tools

There are several numbers of great companion PC tools from third-party.

* [GoVNA](https://github.com/momentics/GoVNA): A robust and secure Go library for controlling NanoVNA vector network analyzers. Features multi-protocol support (V1, V2/LiteVNA), automatic device detection, and optimized for high-performance server-side applications.
* [PyVNA](https://github.com/momentics/PyVNA): Multi-protocol Python library for NanoVNA V1/V2/LiteVNA, derived from GoVNA for high performance & secure server-side applications. Supports auto-detection, robust device pooling, and comprehensive data handling.
* [NanoVNA-App software](https://github.com/OneOfEleven/NanoVNA-H/blob/master/Release/NanoVNA-App.rar) by OneOfEleven

## Documentation

* [NanoVNA-X menu & user workflow reference](doc/menu_and_user_guide.md)
* [NanoVNA User Guide(ja)](https://cho45.github.io/NanoVNA-manual/) by cho45. [(en:google translate)](https://translate.google.com/translate?sl=ja&tl=en&u=https%3A%2F%2Fcho45.github.io%2FNanoVNA-manual%2F)
* [NanoVNA user group](https://groups.io/g/nanovna-users/topics) on groups.io.

## Reference

* [Schematics](/doc/nanovna-sch.pdf)
* [PCB Photo](/doc/nanovna-pcb-photo.jpg)
* [Block Diagram](/doc/nanovna-blockdiagram.png)

## Note

Hardware design material is disclosed to prevent bad quality clone. Please let me know if you would have your own unit.

## Credit
* [@momentics](https://github.com/momentics/) – NanoVNA-X maintainer.

## Based on code from:
* [@DiSlord](https://github.com/DiSlord/) – original NanoVNA-D firmware author whose
  work forms the foundation of this project and remains licensed under the GNU GPL.
* [@edy555](https://github.com/edy555)

