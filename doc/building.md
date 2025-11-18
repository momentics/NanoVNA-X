# Building the NanoVNA-X firmware
[Русская версия](building_ru.md)

This guide documents a reproducible path to build the NanoVNA-X firmware from
source. The steps below have been verified on macOS, Ubuntu/Debian and inside
a GitHub Actions runner. Other Unix-like environments should work as long as
the same ARM GCC cross-compiler is available.

## 1. Install prerequisites

NanoVNA-X targets ARM Cortex-M microcontrollers and therefore requires the
`arm-none-eabi` toolchain plus `dfu-util` if you plan to flash hardware.

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi dfu-util
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

Verify that the compiler is available:

```bash
arm-none-eabi-gcc --version
```

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
| `include/app`, `src/app` | Application-facing APIs (sweep service, shell, measurement pipeline, UI glue). |
| `include/services`, `src/services` | Cross-cutting helpers such as the configuration service, scheduler, and event bus. |
| `include/system`, `src/system` | Low-level components like `state_manager`, which owns persistence, backup registers, and delayed flash writes. |
| `src/platform`, `boards/STM32F0/STM32F3` | Board support packages shared with ChibiOS. |

When adding new code or porting patches, drop each feature into the matching layer to keep dependencies one-directional.

## 3. Choose a target board

The firmware can be built for two board profiles. Select the target by setting
the `TARGET` environment variable before invoking `make`:

| Target | Board                | MCU profile |
|--------|----------------------|-------------|
| F072   | NanoVNA-H (default)  | STM32F072   |
| F303   | NanoVNA-H4           | STM32F303   |

If you omit `TARGET`, the build defaults to `F072`.

## 4. Build the firmware

Use the standard GNU Make workflow:

```bash
export TARGET=F072   # or F303
make clean           # optional, but recommended between target switches
make -j$(nproc)
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

## 5. Flash the device (optional)

With the hardware in DFU mode, flash the generated binary using `dfu-util`:

```bash
export TARGET=F072
make flash
```

The `flash` target always uses the binary generated in `build/` for the
selected board profile. The device must enumerate as a DFU interface on your
host; see the main README for more detail on entering DFU mode.

## 6. Troubleshooting

- **Missing compiler** – confirm that `arm-none-eabi-gcc` is on `PATH` and the
  packages listed above are installed.
- **Old build artefacts** – run `make clean` when switching between targets or
  after updating the toolchain to avoid mixing incompatible object files.
- **Permission errors when flashing** – on Linux, add udev rules for DFU devices
  or run `dfu-util` with `sudo`.
- **Host utility cannot detect the device** – after flashing, open the USB console
  and confirm the session starts with `ch> ` before the banner. Older host caches
  (e.g. NanoVNA-Saver) may need a reconnect to pick up the new handshake.


Following these steps should yield a repeatable build both locally and in
continuous integration environments.
