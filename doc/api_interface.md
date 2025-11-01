# NanoVNA-X USB Communication Protocol Specification

## 1. Transport overview
NanoVNA-X exposes a USB CDC ACM interface implemented by the ChibiOS serial-over-USB stack. The device enumerates with vendor ID `0x0483` (STMicroelectronics) and product ID `0x5740`. The default device release is `0x0200`. One CDC communication interface and one CDC data interface are provided. Endpoint assignments are:

| Endpoint | Direction | Type      | Max packet | Purpose |
|---------:|-----------|-----------|------------|---------|
| 0x01     | OUT       | Bulk      | 64 bytes   | Host-to-device command stream |
| 0x81     | IN        | Bulk      | 64 bytes   | Device-to-host replies and data |
| 0x82     | IN        | Interrupt | 8 bytes    | CDC notification endpoint |

The device strings identify the manufacturer as `nanovna.com`, the product as the configured NanoVNA model, and the serial number can be generated from the MCU unique ID when `__USB_UID__` is enabled.

## 2. Session framing
The USB CDC link presents itself as a virtual serial port. No manual baud-rate configuration is required, but ASCII commands and responses are framed by carriage return + line feed (`\r\n`). The firmware echoes user input and displays the prompt `ch> ` when it is ready to accept a command. Upon connection the initial bytes are `\r\nch> \r\nNanoVNA Shell\r\nch> ` so that host auto-detection logic can key off the prompt before reading the banner text.

Commands are case-sensitive. Arguments are separated by spaces or tabs. Double quotes may be used to pass arguments containing whitespace. Numeric parsing accepts decimal notation by default and supports optional prefixes `0x` (hex), `0o` (octal), `0b` (binary), as well as engineering suffixes such as `k`, `M`, `G`, `m`, `u`, and `n` for floating-point values.

Invalid commands cause the firmware to print `<name>?` followed by a newline. Usage hints are printed for malformed arguments.

## 3. Command lifecycle and concurrency
Each shell command entry defines flag bits that affect execution:

* `CMD_WAIT_MUTEX` — the command is executed by the main UI thread after measurement tasks release shared state. From the host perspective, the command blocks until the response and prompt return.
* `CMD_BREAK_SWEEP` — the command requests the sweep engine to pause so that the operation can complete deterministically.
* `CMD_RUN_IN_UI` — the command is allowed to run even if the on-device UI currently owns the sweep mutex.
* `CMD_RUN_IN_LOAD` — the command can be executed while configuration files are being loaded at boot.

When a command modifies sweep settings it typically pauses generation, performs the change, and optionally resumes. Long-running commands (e.g., sweeping or file I/O) do not send additional acknowledgements; wait for the prompt before issuing the next command.

## 4. Binary data formats
Most replies are textual. When binary data is transmitted, the firmware writes raw memory structures through the shell stream without additional framing:

* Integers and bit-fields follow the MCU endianness (little-endian on STM32).
* Frequencies (`freq_t`) are 32-bit unsigned integers in Hz.
* Complex samples are IEEE-754 32-bit floats in `[real, imag]` order.
* Remote desktop rectangles use the packed `remote_region_t` structure (`char new_str[6]; int16_t x, y, w, h`).

The host must know the expected payload length for each binary-producing command.

## 5. Command reference
Commands are listed alphabetically within thematic groups. Optional commands are gated by compile-time macros noted in parentheses.

### 5.1 Sweep configuration and measurement control
* `bandwidth {count} | bandwidth {frequency_Hz} {measured_bw}` — Set or query the IF bandwidth. With one argument, the raw count (0–511) is applied; with two arguments, the firmware computes the nearest count for the requested Hertz value. Response echoes the active count and effective bandwidth.
* `config {auto|avg|connection|mode|grid|dot|bk|flip|separator|tif} {0|1}` (`ENABLE_CONFIG_COMMAND`) — Enable or disable UI and acquisition modes. `connection` switches between USB and UART console, `bk` toggles RTC backup usage, etc. Responds with usage text if arguments are invalid.
* `freq {frequency_Hz}` — Switch to CW mode at the specified frequency. The sweep pauses while the generator retunes.
* `measure {mode}` (`__VNA_MEASURE_MODULE__`) — Select measurement post-processing (e.g., `lc`, `filter`, `cable`). Usage text is printed for unsupported modes.
* `offset {frequency_offset_Hz}` (`USE_VARIABLE_OFFSET`) — Apply a global generator offset in Hz.
* `pause` — Pause the sweep engine immediately.
* `power {0-3|255}` — Set the Si5351 output drive strength (255 enables automatic level control). Responds with the current selection if arguments are missing.
* `resume` — Rebuild sweep frequency tables and resume continuous sweeping.
* `scan {start_Hz} {stop_Hz} [points] [mask]` — Execute a sweep over the requested range. `points` defaults to the current resolution. `mask` selects the response content (see below). Without `mask`, the command only updates internal buffers.
* `scan_bin ...` (`ENABLE_SCANBIN_COMMAND`) — Force binary output for the subsequent `scan` invocation by setting `SWEEP_BINARY` before delegating to `scan`.
* `smooth {0-8}` (`__USE_SMOOTH__`) — Control moving-average smoothing of measured data.
* `sweep {start_Hz} [stop_Hz] [points]` — Set sweep boundaries and optional point count. Alternatively use `sweep {start|stop|center|span|cw|step|var} {value}` to adjust a single parameter.
* `tcxo {frequency_Hz}` — Configure the external TCXO frequency.
* `threshold {frequency_Hz}` — Update the harmonic mode crossover threshold.
* `transform {on|off|impulse|step|bandpass|mininum|normal|maximum}` (`ENABLE_TRANSFORM_COMMAND`) — Toggle time-domain transform and windowing.

**Scan mask bits** (combine via addition or bitwise OR):

| Bit | Value | Meaning |
|----:|------:|---------|
| 0   | 0x01  | Include frequency values |
| 1   | 0x02  | Include channel 0 data (S11) |
| 2   | 0x04  | Include channel 1 data (S21) |
| 3   | 0x08  | Ignore stored calibration |
| 4   | 0x10  | Ignore electrical delay correction |
| 5   | 0x20  | Ignore S21 magnitude offset |
| 7   | 0x80  | Emit binary payload instead of text |

When the binary bit is set, the reply starts with the 16-bit mask and 16-bit point count, followed by the requested records in the order listed above. Each frequency is a 32-bit `freq_t`; each complex sample is two floats.

### 5.2 Data access
* `capture [rle]` — Dump the LCD framebuffer. Without arguments, `LCD_WIDTH × LCD_HEIGHT × 2` bytes are streamed in RGB565 order, row-major. With any argument and `__CAPTURE_RLE8__` enabled, the firmware prepends a BMP-style header, palette block length, the palette itself, and PackBits-compressed rows.
* `data [index]` — Emit the latest complex data. Index `0` selects live S11, `1` selects live S21, and `2…6` select stored calibration arrays (`load`, `open`, `short`, `thru`, `isoln`). Each line contains `real imag` floats.
* `frequencies` — Print the active sweep frequency list, one Hz value per line.
* `scan` — See Section 5.1.

### 5.3 Calibration, traces, and markers
* `cal` — Without arguments, list calibration steps that have been completed. With arguments: `load`, `open`, `short`, `thru`, and `isoln` capture the respective standards; `done` finalizes error-term computation; `on`/`off` toggle calibration usage; `reset` clears all steps.
* `edelay [s11|s21] {picoseconds}` — Query or set channel-specific electrical delay in picoseconds (values are stored internally as seconds).
* `marker` — Without arguments, list enabled markers with their index and frequency. Use `marker on|off` to toggle all markers, `marker {n}` to select marker `n`, `marker {n} on|off` to enable or disable a specific marker, or `marker {n} {index}` to move the marker to a sweep index.
* `s21offset {dB}` — Query or set the logarithmic offset applied to S21 magnitude display.
* `save {slot}` / `recall {slot}` — Store or restore calibration and user preferences in numbered slots.
* `trace` — Manage trace rendering. `trace` alone lists enabled traces with their type, channel, scale, and reference position. `trace {n}` prints the type/channel for trace `n`. `trace {n} off` disables a trace. `trace {n} {type} [channel]` assigns a new measurement type. `trace {n} scale {value}` and `trace {n} refpos {value}` adjust display parameters. Smith chart format adjustments accept values such as `lin`, `ri`, `rlc`, etc.

### 5.4 Storage and configuration persistence
* `clearconfig {1234}` — Factory reset of persistent configuration and calibration (requires the literal key `1234`).
* `saveconfig` — Persist the current configuration.

### 5.5 System information and diagnostics
* `band {idx} {mode} {value}` (`ENABLE_BAND_COMMAND`) — Update Si5351 band-configuration tables.
* `dac {0-4095}` (`__VNA_ENABLE_DAC__`) — Program the auxiliary DAC output.
* `gain {lgain} [rgain]` (`ENABLE_GAIN_COMMAND`) — Adjust TLV320AIC3204 PGA gains.
* `info` (`ENABLE_INFO_COMMAND`) — Print the null-terminated `info_about[]` strings that describe the firmware build.
* `reset [dfu]` — Perform a software reset, optionally entering DFU boot mode when compiled with `__DFU_SOFTWARE_MODE__`.
* `stat` (`ENABLE_STAT_COMMAND`) — Capture raw ADC samples and report channel averages and RMS values.
* `tcxo`, `threshold`, `version`, `vbat`, `vbat_offset` — See Sections 5.1 and 5.3 for related behaviour. `version` prints `NANOVNA_VERSION_STRING`; `vbat` reports the instantaneous battery voltage in millivolts; `vbat_offset` gets or sets the correction offset.
* `threads` (`ENABLE_THREADS_COMMAND`) — Dump ChibiOS thread diagnostics, including stack usage and priorities.
* `time` (`__USE_RTC__`) — Query or set the RTC. `time` alone prints the current date/time. `time b {YYMMDD_hex} {HHMMSS_hex}` writes raw registers. Individual fields (e.g., `y`, `m`, `d`, `h`, `min`, `sec`, `ppm`) can be updated with decimal values.
* `usart_cfg {baud}` / `usart {string} [timeout_ms]` (`ENABLE_USART_COMMAND` and `__USE_SERIAL_CONSOLE__`) — Reconfigure or use the hardware UART console. `usart` sends the string (plus newline) to the UART and streams any reply back over USB until the timeout expires.

### 5.6 Remote UI and automation
* `refresh {on|off}` (`__REMOTE_DESKTOP__`) — Enable (`on`) or disable (`off`) remote screen streaming. When enabled and the USB CDC link is active, the firmware periodically sends a `remote_region_t` header followed by pixel data for regions that changed, then terminates the update with the normal prompt.
* `touch {x} {y}` / `release [x y]` (`__REMOTE_DESKTOP__`) — Inject remote touch-press or touch-release events. Passing `-1` for a coordinate preserves the last position.
* `touchcal`, `touchtest` — Trigger on-device touch calibration or diagnostics.

### 5.7 Developer utilities
* `dump [selection]` (`ENABLED_DUMP_COMMAND`) — Capture raw I/Q samples for debugging.
* `i2c {page} {reg} {data}` (`ENABLE_I2C_COMMAND`) — Write to the TLV320AIC3204 codec registers.
* `i2c {timing}` (`ENABLE_I2C_TIMINGS`) — Low-level reprogramming of I²C timing registers.
* `lcd {cmd} [byte ...]` (`ENABLE_LCD_COMMAND`) — Send raw commands to the LCD controller and report the return status.
* `sample {gamma|ampl|ref}` (`ENABLE_SAMPLE_COMMAND`) — Switch the diagnostic sample source used by some measurements.
* `si {reg} {value}` (`ENABLE_SI5351_REG_WRITE`) — Write directly to Si5351 registers.
* `test` (`ENABLE_TEST_COMMAND`) — Reserved for ad-hoc diagnostics (no default output).

### 5.8 Help and discovery
* `help` — Print the list of registered commands.
* `version`, `info`, and `help` are always safe to call immediately after connecting.

## 6. Example exchanges
1. **Query firmware version**
   ```text
   ch> version\r\n
   NanoVNA-X ...\r\n
   ch> 
   ```
2. **Perform a 201-point sweep and retrieve textual data**
   ```text
   ch> sweep 50000000 150000000 201\r\n
   ch> scan 50000000 150000000 201 0x03\r\n
   50000000 0.123456 -0.234567 0.345678 -0.456789\r\n
   ...
   ch> 
   ```
3. **Retrieve binary sweep data**
   *Send:* `scan 50000000 150000000 201 0x83\r\n`
   *Receive:* first four bytes contain the mask `0x0083` and point count `0x00C9` (201), followed by 201 records of frequency (`uint32_t`) and selected complex samples (`float[2]`).
4. **Capture the LCD as raw RGB565**
   ```text
   ch> capture\r\n
   <LCD_WIDTH * LCD_HEIGHT * 2 bytes of pixel data>ch>
   ```

## 7. Error handling and best practices
* Wait for the `ch> ` prompt before sending the next command. Commands that manipulate sweeps or storage may take several hundred milliseconds.
* Validate binary payload lengths on the host side—no additional delimiters are appended.
* For long textual transfers (`scan` without binary, `data`, `frequencies`), throttle host-side parsing if necessary; the firmware yields periodically but does not pace output.
* Use `pause`/`resume` or `scan` with masks to avoid interfering with an in-progress sweep when running automated test suites.

## 8. Optional feature summary
The availability of certain commands depends on compile-time options. Key macros include:

| Macro | Enables |
|-------|---------|
| `__REMOTE_DESKTOP__` | Remote framebuffer streaming (`refresh`, `touch`, `release`). |
| `__VNA_MEASURE_MODULE__` | Advanced `measure` modes. |
| `__USE_SMOOTH__` | `smooth` command. |
| `ENABLE_SCANBIN_COMMAND` | Binary `scan` helper. |
| `ENABLE_CONFIG_COMMAND` | `config` console toggles. |
| `ENABLE_USART_COMMAND` & `__USE_SERIAL_CONSOLE__` | UART bridging commands. |
| `ENABLE_*` families | Diagnostic utilities (`gain`, `stat`, `threads`, etc.). |

Consult the firmware configuration when integrating to ensure the desired features are present.
