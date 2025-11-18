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
memory-constrained STM32F072 and STM32F303 platforms. While the project was initially bootstrapped
from the public NanoVNA firmware, the current codebase is a purpose-built architecture focused on
responsiveness, deterministic sweeps, and clean subsystem boundaries. The firmware remains GPLv3 so
that every contributor, past and present, keeps their rights.

This repository contains the source code of the improved NanoVNA-H and NanoVNA-H4 firmware
used in the NanoVNA-X project. The documentation describes the build and flash process on a
macOS or a Linux (Debian or Ubuntu) system, other Linux (or even BSD) systems may behave
similar.

## Acknowledgment and Disclaimer

- Respect to [@edy555](https://github.com/edy555) and [@DiSlord](https://github.com/DiSlord) for open-sourcing the original NanoVNA firmware; their work made a community-grade VNA possible in the first place.
- NanoVNA-X has since been re-architected and the sweep/UI subsystems now share very little code with the legacy tree; every release deliberately moves further away from the blocking design.
- The goal is to keep the familiar UX while delivering a maintainable, modular implementation with first-class SD features (S‑parameter capture, screenshots, firmware dumps, scripted commands, on-device formatting).

## What Makes NanoVNA-X Different

- **Predictable, event-driven architecture.** The sweep engine, UI, USB CDC shell, and measurement DSP cooperate through the ChibiOS event bus and watchdog-guarded timeouts. A hung codec, synthesiser, or PC host can no longer freeze the instrument mid-calibration.
- **Measurement-focused DMA budget.** SPI LCD transfers and TLV320 I²S captures use DMA, while the UART console was intentionally moved to an IRQ driver so that DMA channels are always available for RF data paths.
- **Unique USB identity by default.** Every unit now enumerates with a serial number derived from its MCU unique ID (System -> Device -> MORE -> *USB DEVICE UID* still allows opting out for legacy workflows).
- **Integrated SD workflow.** The internal micro-SD slot supports calibration slots, S1P/S2P exports, BMP or compact RLE-TIFF screenshots, firmware dumps, scripted command playback, and a deterministic `FORMAT SD` routine that uses FatFs mkfs parameters aligned with Keysight/R&S service practices.

## Improvements
The firmware was rewritten around a layered ChibiOS stack that emphasises responsiveness, standalone usability, and reliable sweeps on constrained hardware.

### Firmware Stability and Responsiveness
The core of the firmware was reworked from blocking calls to a fully asynchronous, event-driven architecture using ChibiOS primitives (Mailboxes, Events, and Semaphores).
* **Non-Blocking USB:** The USB CDC (serial) stack is now fully asynchronous. The firmware no longer hangs if a host PC connects, disconnects, or stalls during a data transfer. This resolves the most common source of device freezes.
* **Timeout-Driven Recovery:** Critical subsystems, including the measurement engine and I²C bus, are guarded by timeouts. A stalled operation will no longer lock up the device; instead, the subsystem attempts to recover cleanly.
* **RTOS-based Concurrency:** Busy-wait loops and polling have been replaced with efficient RTOS-based signaling, reducing CPU load and improving battery life. The measurement thread, UI, and USB stack now cooperate without race conditions or deadlocks.
* **Persistent State:** A dedicated `system/state_manager` module snapshots sweep limits, UI flags, and calibration slots to flash. Changes are auto-saved after you stop editing (or immediately via *Save Config*), so editing the sweep on the device or over USB survives a cold boot without hammering flash.
* **UI/Sweep Sync:** The UI and sweep engine are now decoupled. The UI remains responsive even during complex calculations, and on-screen data is always synchronized with the underlying measurement state.

### Performance and Resource Management
* **Targeted Memory Optimization:** Static RAM consumption has been significantly reduced, especially for the 16 kB STM32F072 (NanoVNA-H) target. This was achieved by tuning buffer sizes, disabling features like trace caching on low-memory models, and moving key buffers to CCM RAM on the STM32F303.
* **Strategic DMA Usage:** The DMA architecture was refined to prioritize measurement stability. DMA is used for the most demanding data paths:
    *   **LCD Interface (SPI):** Ensures smooth, high-speed UI and graph rendering.
    *   **Measurement Pipeline (I²S):** Guarantees sample delivery from the codec without dropping data.
    *   To free up DMA channels for these critical tasks, the **UART console was intentionally moved to a non-DMA, interrupt-driven driver.** This prevents resource conflicts and prioritizes measurement integrity.

### Sweep Throughput (STM32F072 reference)
Measured on production NanoVNA-H hardware with a 101-point sweep:

| Scenario | Effective throughput |
| --- | --- |
| 50 kHz–900 MHz span without harmonic retune breaks | 138–142 points per second (variance stems from Si5351 PLL settling time). |
| 50 kHz–2.7 GHz full span with harmonic retunes (50–300 / 300–600 / 600–900 MHz fundamental bands plus harmonic hops) | 165–172 points per second thanks to the pipelined sweep engine and optimised IF bandwidth scheduling. |

STM32F303-based NanoVNA-H4 units benefit from the larger SRAM pool and typically exceed these numbers, but the F072 figures above define the guaranteed baseline for field deployments.

### New Features and Capabilities
* **PLL Stabilization:** The Si5351 synthesizer programming sequence was optimized to reduce frequency overshoot and drift, improving measurement repeatability, especially at the start of a sweep.
* **Deterministic USB serial numbers:** The firmware now enables the USB unique-ID mode at boot, so every unit enumerates with a stable serial number that host software (NanoVNA-App, NanoVNA Saver, etc.) can display without extra configuration.

### Build and Development Workflow
* **Automated Versioning:** The firmware version is now automatically embedded during the build process from a single `VERSION` file.
* **Improved CI/CD:** The GitHub Actions workflows have been optimized for faster, more reliable builds with better caching.
* **DFU File Generation:** A Python-based script for creating DfuSe-compatible firmware files has been integrated into the release process.

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
`EVENT_USB_COMMAND_PENDING`) cover the current coordination needs; adding new topics
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

## Repository layout

The source tree has been reorganised so it is no longer a line-for-line fork of the legacy NanoVNA firmware:

| Path | Purpose |
| --- | --- |
| `include/app`, `src/app` | Application-facing APIs: the sweep service, shell, measurement pipeline, and UI glue. |
| `include/services`, `src/services` | Cross-cutting infrastructure (config service, scheduler, event bus). |
| `include/system`, `src/system` | Platform-level building blocks such as the new `state_manager` that owns persistence, backup registers, and autosave scheduling. |
| `src/platform`, `boards/STM32F0/STM32F3` | Low-level board support code shared with ChibiOS. |

This separation lets you trace dependencies easily (e.g. `ui/` depends on `system/state_manager.h` but not vice versa) and removes duplicated helper tables from multiple files. When porting patches from older firmware trees, map the functionality onto the closest module instead of copying entire source files.

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
* Select menu System -> Device -> DFU (requires recent firmware).
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
* [NanoVNA-App software](https://github.com/OneOfEleven/NanoVNA-App) by OneOfEleven

## Documentation

* [NanoVNA-X menu structure](doc/menu_structure.md)
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
* [@momentics](https://github.com/momentics/) – NanoVNA-X maintainer and integrator.
* The broader NanoVNA community – especially [@edy555](https://github.com/edy555) and [@DiSlord](https://github.com/DiSlord) – for releasing the original schematics, DSP tables, and firmware under the GPL.
