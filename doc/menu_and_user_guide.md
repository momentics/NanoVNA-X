# NanoVNA-X Menu Reference

This document describes the on-device menu tree implemented in the current NanoVNA-X firmware. The structure and behaviour below are taken directly from [`src/ui/ui.c`](../src/ui/ui.c) and associated helpers. Items guarded by compile-time feature switches are called out explicitly so you can match the build configuration in `include/nanovna.h`.

## Navigation basics

* Each submenu automatically appends a `← BACK` entry. Activating it returns to the previous level without applying extra actions.
* Buttons that show a checkbox icon toggle a firmware flag immediately. Entries rendered with a highlighted background indicate context-sensitive information (for example, the active trace colour).
* Numeric fields open the on-screen keypad. The keypad honours engineering suffixes (`k`, `M`, `G`, etc.) and uses the currently selected digit separator when that feature is enabled.

## Top-level menu bar

| Entry | Purpose |
| --- | --- |
| **DISPLAY** | Presentation controls for traces, formats, scaling and DSP helpers. |
| **MARKER** | Marker enablement, search, tracking and marker-driven operations. |
| **STIMULUS** | Sweep range, continuous-wave frequency and point-count settings. |
| **CALIBRATE** | Calibration workflow, slot management and correction flags. |
| **RECALL** | Load previously saved calibration data. |
| **MEASURE** | (Enabled by `__VNA_MEASURE_MODULE__`.) Access context-aware measurement assistants. |
| **SD CARD** | (Enabled by `__USE_SD_CARD__`.) File browser, import/export and screenshot controls. |
| **CONFIG** | Device configuration, expert tools and utilities. |
| **PAUSE/RESUME SWEEP** | Toggles the background sweep (`PAUSE` when running, `RESUME` when halted). |

---

## DISPLAY menu

* **TRACE** → opens a list of `TRACE 0` … `TRACE 3` entries. Selecting a trace either toggles its visibility or makes it the active trace if it is already enabled. When `STORED_TRACES` is greater than zero, additional entries labelled `STORE TRACE`/`CLEAR TRACE` appear for each stored-trace slot.
* **FORMAT S11 (REFL)** → lists every S11 format for the active trace:
  * `LOGMAG`, `PHASE`, `DELAY`, `SMITH`, `SWR`, `RESISTANCE`, `REACTANCE`, `|Z|`.
  * `MORE` → exposes additional S11 formats: `POLAR`, `LINEAR`, `REAL`, `IMAG`, `Q FACTOR`, `CONDUCTANCE`, `SUSCEPTANCE`, `|Y|`.
  * A second `MORE` branch adds impedance/admittance transformations such as `Z PHASE`, `SERIES C/L`, `PARALLEL R/X/C/L`.
  * Selecting `SMITH` twice opens a Smith readout submenu whose contents depend on the trace channel (`LIN`, `LOG`, `RE/IM`, and impedance/admittance presentations for S11 or S21).
* **FORMAT S21 (THRU)** → mirrors the S21 formats (`LOGMAG`, `PHASE`, `DELAY`, `SMITH`, `POLAR`, `LINEAR`, `REAL`, `IMAG`) and a `MORE` branch containing series/shunt resistance, reactance and Q-factor views.
* **CHANNEL** → toggles the active trace between reflection (CH0) and transmission (CH1).
* **SCALE** → provides `AUTO SCALE`, manual `TOP`, `BOTTOM`, `SCALE/DIV`, `REFERENCE POSITION`, `E-DELAY` entry fields, `S21 OFFSET`, and (when `__USE_GRID_VALUES__` is defined) `SHOW GRID VALUES` plus `DOT GRID` toggles.
* **TRANSFORM** → controls time-domain transforms: enable/disable, select `LOW PASS IMPULSE`, `LOW PASS STEP` or `BANDPASS`, choose the window function, and edit the velocity factor.
* **IF BANDWIDTH** → presents the hardware-specific list of IF bandwidth presets compiled into the firmware (for example 8 kHz, 4 kHz … down to 10 Hz on supported platforms).
* **DATA SMOOTH** *(optional)* → appears with `__USE_SMOOTH__`. Lets you toggle smoothing off or pick from the compiled averaging factors.
* **PORT-Z** *(optional)* → available when `__VNA_Z_RENORMALIZATION__` is defined. Sets a custom reference impedance for S11 processing.

## MARKER menu

* **SELECT MARKER** → lists every marker slot allowed by `MARKERS_MAX`. Tapping an entry toggles the marker. The currently active marker shows a combined check/arrow icon. Two utility buttons follow the marker list: `ALL OFF` clears every marker, and `DELTA` toggles delta marker readouts.
* **SEARCH [ON/OFF]** → toggles automatic peak/valley search mode.
* **SEARCH ← LEFT / SEARCH → RIGHT** → step the highlighted marker to the next result in the chosen direction.
* **OPERATIONS** → submenu containing `→ START`, `→ STOP`, `→ CENTER`, `→ SPAN` and `→ E-DELAY`. The sweep range buttons copy the active marker (and, when available, the previously active marker) into the corresponding sweep limits. `E-DELAY` adds the marker’s measured group delay to the active trace channel.
* **TRACKING** → toggles marker tracking so the active marker follows live searches.

## STIMULUS menu

* **START**, **STOP**, **CENTER**, **SPAN** → keypad-driven sweep limit controls.
* **CW FREQ** → sets a fixed continuous-wave output while the sweep remains paused.
* **FREQ STEP** → edits the coarse tuning step used by the leveler/jog controls.
* **JOG STEP** → switches between automatic and manual jog increments.
* **SWEEP POINTS** → displays the current sweep size and opens a submenu. The submenu offers a direct `SET POINTS` keypad entry plus every preset enumerated in `POINTS_SET` for the target board.

## CALIBRATE menu

* **CALIBRATE** → opens the calibration assistant with the ordered steps `OPEN`, `SHORT`, `LOAD`, `ISOLN`, `THRU`, followed by `DONE` (store to flash) and `DONE IN RAM` (apply without persistence). Completed steps show a check mark.
* **POWER AUTO** → opens a drive-level submenu. You can force specific Si5351 drive strengths (2 mA through 8 mA) or revert to automatic control.
* **SAVE** → submenu used to archive calibrations. Each slot shows the stored frequency span when populated. When SD support and the browser are enabled a `SAVE TO SD CARD` item is prepended.
* **RANGE** → displays the point count and frequency span of the loaded calibration and, when invoked, re-applies those limits and power settings if the calibration was interpolated.
* **RESET** → clears the current calibration (except for the enhanced-response flag).
* **APPLY** → toggles correction on/off without discarding coefficients.
* **ENHANCED RESPONSE** → enables or disables the enhanced-response correction path.
* **STANDARD LOAD R** *(optional)* → available when `__VNA_Z_RENORMALIZATION__` is defined. Lets you redefine the nominal load resistance used during calibration math.

## RECALL menu

* Lists every calibration slot with its stored span metadata. Selecting a populated slot loads it and highlights it as the active save ID.
* When SD card browsing is enabled a `LOAD FROM SD CARD` entry at the top reads calibration archives directly.

## MEASURE menu (`__VNA_MEASURE_MODULE__`)

Pressing **MEASURE** opens the submenu tied to the currently active measurement mode.

* **General list** (accessible when no specialised view is active): `OFF`, `L/C MATCH`, `CABLE (S11)`, `RESONANCE (S11)`, `SHUNT LC (S21)`, `SERIES LC (S21)`, `SERIES XTAL (S21)`, `FILTER (S21)`.
* Selecting a mode switches to one of the specialised menus:
  * **L/C MATCH** → `OFF`, `L/C MATCH`.
  * **Cable (S11)** → `OFF`, `CABLE (S11)`, `VELOCITY FACTOR`, `CABLE LENGTH`.
  * **Resonance (S11)** → `OFF`, `RESONANCE (S11)`.
  * **S21 Shunt/Series/XTAL** → `OFF`, `SHUNT LC (S21)`, `SERIES LC (S21)`, `SERIES XTAL (S21)`, and an editable `Rl` load value.
  * **Filter (S21)** → `OFF`, `FILTER (S21)`.

## SD CARD menu (`__USE_SD_CARD__`)

* **LOAD** *(shown when `__SD_FILE_BROWSER__` is enabled)* → opens the SD browser filtered to screenshots (`LOAD SCREENSHOT`), `S1P`, `S2P` or calibration files.
* **SAVE S1P / SAVE S2P** → exports the current data set using either automatic filenames or the keypad prompt, depending on the `AUTO NAME` flag.
* **SCREENSHOT** → captures the current LCD contents (BMP by default, or TIFF when that format is selected).
* **SAVE CALIBRATION** → stores the active calibration to removable media.
* **AUTO NAME** → toggles timestamp-based filenames for exports.
* **IMAGE FORMAT** *(available with `__SD_CARD_DUMP_TIFF__`)* → switches the screenshot encoder between BMP and TIFF.

## CONFIG menu

* **TOUCH CAL** → launches the touch-screen calibration routine.
* **TOUCH TEST** → draws live feedback for verifying touch input.
* **EXPERT SETTINGS** → opens advanced hardware controls:
  * `THRESHOLD` (magnitude threshold), `TCXO` (reference oscillator trim) and `VBAT OFFSET` (battery calibration) all use the keypad.
  * `IF OFFSET` *(when `USE_VARIABLE_OFFSET_MENU` is defined)* → opens a submenu listing the available IF offset presets.
  * `REMEMBER STATE` *(with `__USE_BACKUP__`)* → toggles retention of UI state across restarts.
  * `FLIP DISPLAY` *(with `__FLIP_DISPLAY__`)* → mirrors the LCD orientation.
  * `→ DFU` *(with `__DFU_SOFTWARE_MODE__`)* → jumps to the DFU bootloader.
  * `→ MORE` → exposes manufacturer-level tools:
    * `MODE` cycles the Si5351 generator variant (`Si5351`, `MS5351`, `SWC5351`).
    * `SEPARATOR` *(with `__DIGIT_SEPARATOR__`)* toggles between dot and comma numeric separators.
    * `USB DEVICE UID` *(with `__USB_UID__`)* displays the unique USB identifier.
    * `DUMP FIRMWARE` *(with `__SD_CARD_DUMP_FIRMWARE__`)* writes the flash image to the SD card.
    * `LOAD COMMAND SCRIPT` *(with both `__SD_CARD_LOAD__` and the browser enabled)* launches the file browser for command scripts. Builds without the browser expose `LOAD CONFIG.INI` instead.
    * `CLEAR CONFIG` → submenu containing `CLEAR ALL AND RESET`, which wipes configuration storage.
* **SAVE CONFIG** → commits the in-memory configuration to non-volatile storage.
* **CONNECTION** *(present with `__USE_SERIAL_CONSOLE__`)* → allows toggling the USB/serial console backend and choosing among the predefined UART bit rates.
* **VERSION** → shows firmware version details.
* **BRIGHTNESS** *(with `__LCD_BRIGHTNESS__`)* → interactive slider for the backlight level.
* **DATE/TIME** *(with `__USE_RTC__`)* → submenu providing `SET DATE`, `SET TIME`, `RTC CAL` (ppm trim) and `RTC 512 Hz / LED2` output control.

## Sweep control button

The final menu button reflects the current sweep state. It reads `PAUSE SWEEP` when the VNA is sweeping and `RESUME SWEEP` once paused. Activating the button calls `toggle_sweep()` and updates the label/icon accordingly.

---

This reference mirrors the firmware defaults in `include/nanovna.h` and `include/app.app_features.h`.
If you disable a feature flag during compilation, the associated menu entries are omitted automatically.
