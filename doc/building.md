# Building the NanoVNA-X firmware

[Русская версия](building_ru.md)

This guide documents a reproducible path to build the NanoVNA-X firmware from
source. CI and release builds use the official Linux x86_64 Arm GNU
14.2.Rel1 package (GCC 14.2.1), including its runtime libraries. Use the same
package in Linux/WSL when comparing firmware sizes; distribution packages can
produce different images even when their GCC versions match.

## 1. Install prerequisites

NanoVNA-X targets ARM Cortex-M microcontrollers and therefore requires the
`arm-none-eabi` toolchain plus `dfu-util` if you plan to flash hardware.

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install build-essential python3 curl ca-certificates xz-utils dfu-util
```

### macOS (Homebrew)

```bash
brew install gcc-arm-none-eabi dfu-util
```

### Windows

Use [WSL2](https://learn.microsoft.com/windows/wsl/install) with an Ubuntu
distribution and follow the Linux instructions above. Native Windows builds are
possible with the ARM GNU toolchain archive from Arm, but the project is tested
on Unix environments.

## 2. Fetch the source code

Clone the repository; all third-party code is vendored directly in-tree:

```bash
git clone https://github.com/momentics/NanoVNA-X.git
cd NanoVNA-X
```

### 2.1 Inspect the source tree

The codebase is now split into clearly defined layers rather than the historical monolithic fork:

| Path | Purpose |
| --- | --- |
| `include/runtime`, `src/runtime` | Application entry points, feature toggles, sweep orchestration, and CLI plumbing. |
| `include/rf`, `src/rf` | Sweep orchestration, measurement pipelines, and RF-side helpers. |
| `include/processing`, `src/processing` | DSP kernels and math helpers backing measurement and UI logic. |
| `include/sys`, `src/sys` | Shared services (configuration persistence, scheduler, event bus, state manager). |
| `include/driver`, `src/driver` | Hardware drivers (LCD, Codec, Synthesizer, USB). |
| `include/ui`, `src/ui` | Rendering, controllers, input adapters, and embedded font/icon assets. |
| `boards/STM32F072`, `boards/STM32F303` | Board support packages shared with ChibiOS. |

When adding new code or porting patches, drop each feature into the matching layer to keep dependencies one-directional.

## 3. Choose a target board

The firmware can be built for two board profiles. Select the target with
`make TARGET=...` or by setting the `TARGET` environment variable:

| Target | Board                | MCU profile |
|--------|----------------------|-------------|
| F072   | NanoVNA-H (default)  | STM32F072   |
| F303   | NanoVNA-H4           | STM32F303   |

If you omit `TARGET`, the build defaults to `F072`.

## 4. Build the firmware

On Linux x86_64 (including x86_64 WSL), install the pinned compiler after
cloning, then use GNU Make:

```bash
bash tools/install-arm-toolchain.sh "$PWD/../arm-gnu-14.2.rel1"
export PATH="$PWD/../arm-gnu-14.2.rel1/bin:$PATH"
```

The installer verifies Arm's SHA-256 checksum before extracting the package and
checks the compiler version. The release, version and checksum are locked in
`tools/arm-toolchain.env`. Both workflows include that file's hash in all build
and compiler cache keys, preventing reuse across toolchain packages. Build cache
keys and fallback prefixes also hash `Makefile` to prevent restoring objects
after build settings change. When changing the pin, validate both F072 and F303
with clean builds and `make test`.

The installer supports Linux x86_64 only. On macOS use the Homebrew compiler
above instead; its output can differ in size from the pinned Linux builds.

Link-time optimization (LTO) defaults to `USE_LTO=yes` for both F072 and F303.
Override it explicitly with `USE_LTO=yes` or `USE_LTO=no` on the Make command
line. Both targets and LTO modes share the same `build/` directory; Make does
not automatically track target or LTO changes. A clean rebuild is required
whenever you switch target, LTO mode, or compiler.

Build either board with LTO enabled (the default):

```bash
make clean
make TARGET=F072 USE_LTO=yes -j$(nproc)

make clean
make TARGET=F303 USE_LTO=yes -j$(nproc)
make test
```

For a non-LTO build, clean first and explicitly disable LTO:

```bash
make clean
make TARGET=F072 USE_LTO=no -j$(nproc)

make clean
make TARGET=F303 USE_LTO=no -j$(nproc)
make test
```

On macOS use `make -j$(sysctl -n hw.ncpu)` for the parallel build step.

The build artefacts are written to `build/`:

- `build/H.elf` / `build/H4.elf` – the ELF image for debugging.
- `build/H.bin` / `build/H4.bin` – the binary file used for DFU flashing.
- `build/H.hex` / `build/H4.hex` – Intel HEX formatted image.

### 4.1 Understand the artefacts

Each generated file serves a slightly different purpose. Use the variant that
matches the tooling available in your lab or production line:

| Artefact        | Typical tooling                                      | When to choose it |
|-----------------|------------------------------------------------------|-------------------|
| `.bin` (raw)    | `dfu-util`, `dfuse-tool`, STM32CubeProgrammer (USB)  | Default choice for field upgrades over USB DFU. It matches the layout expected by `make flash` and by the release DFU packages. |
| `.hex` (Intel)  | ST-Link Utility, STM32CubeProgrammer (SWD/JTAG), embedded production testers | Required when the programming fixture only consumes Intel HEX (text) files or when you want address annotations for manual inspection. |
| `.elf` (debug)  | `gdb-multiarch`, OpenOCD, Lauterbach, on-chip debuggers | Contains symbol tables and section metadata. Use this when stepping through the firmware, generating `.map` files, or investigating crashes. |

Release builds on GitHub publish the same set of files for both board
profiles. They can be flashed as-is without rebuilding locally. The `.dfu`
packages on the release page wrap the `.bin` image together with USB metadata
so that tools such as the Windows `DfuSe` utility recognise the file
immediately; flashing via `make flash` or `dfu-util` works with either the raw
`.bin` or the `.dfu` artefact.

## 5. Memory budgets

The `dump [selection]` command is enabled by default on both targets. It exports
raw audio ADC samples using the existing display scratch buffer, without an
additional sample array. DMA capture waits have a two-second timeout. Disabling
it explicitly with `ENABLED_DUMP_COMMAND=0` in the compiler definitions remains
available for custom builds; the standard F072 image relies on default LTO to fit.

The linker enforces the firmware flash budget before the reserved calibration
area and checks that statically allocated memory fits the target's regions:

| Target | Firmware flash budget | Calibration area starts at | Main SRAM | Separate CCM RAM |
| --- | --- | --- | --- | --- |
| F072 | 98 KiB | `0x08018800` | 16 KiB | None |
| F303 | 114 KiB | `0x0801C800` | 40 KiB | 8 KiB |

These are region limits, not measured firmware sizes or free-memory figures.
The F303's 8 KiB CCM region is separate from its 40 KiB main SRAM and must not
be counted as additional main SRAM. A successful link does not establish
runtime stack headroom; that requires separate runtime measurement and analysis.

## 6. Flash the device (optional)

With the hardware in DFU mode, flash the generated binary using `dfu-util`:

```bash
export TARGET=F072
make flash
```

The `flash` target always uses the binary generated in `build/` for the
selected board profile. The device must enumerate as a DFU interface on your
host; see the main README for more detail on entering DFU mode.

## 7. Troubleshooting

- **Missing compiler** – confirm that `arm-none-eabi-gcc` is on `PATH` and the
  packages listed above are installed.
- **Old build artefacts** – run `make clean` when switching targets, changing
  `USE_LTO`, or updating the toolchain to avoid mixing incompatible object files.
- **Permission errors when flashing** – on Linux, add udev rules for DFU devices
  or run `dfu-util` with `sudo`.
- **Host utility cannot detect the device** – after flashing, open the USB console
  and confirm the session starts with `ch>` before the banner. Older host caches
  (e.g. NanoVNA-Saver) may need a reconnect to pick up the new handshake.

Following these steps should yield a repeatable build both locally and in
continuous integration environments.
