# NanoVNA-X Menu & Workflow Reference

This guide documents the on-device menu tree implemented in the current NanoVNA-X firmware. The hierarchy is sourced from [`src/ui/ui.c`](../src/ui/ui.c); optional entries are noted with their guarding compile-time macros so you can match your build configuration.

## Navigation basics

* Every submenu automatically appends a `← BACK` item that returns to the previous level.
* Buttons with checkboxes toggle immediately; highlighted entries generally expose live status (for example, the colour of the active trace).
* Numeric fields open the on-screen keypad. Engineering suffixes (`k`, `M`, `G`, etc.) and the selected digit separator are honoured.

## Top-level menu bar

| Entry | Purpose |
| --- | --- |
| **CAL** | Keysight/R&S-style calibration hub for standards, ranges, and slot management. |
| **STIMULUS** | Sweep start/stop, CW mode, jog controls, and point-count presets. |
| **DISPLAY** | Trace visibility, formats, scaling, and marker operations. |
| **MEASURE** | DSP helpers (transform, smoothing), IF bandwidth, measurement assistants. |
| **SYSTEM** | Touch utilities, configuration persistence, RTC, and hardware settings. |
| **SD CARD** | Removable-media workflows: save/load, screenshots, firmware dumps, and card maintenance. |
| **PAUSE/RESUME SWEEP** | Toggles the continuous sweep engine (`PAUSE` while running, `RESUME` once halted). |

---

## CAL menu

### MECH CAL
* `MECH CAL` opens the classic calibration assistant with ordered steps `OPEN`, `SHORT`, `LOAD`, `ISOLN`, `THRU`, followed by `DONE` (persist to flash) and `DONE IN RAM` (apply without saving). Completed standards show a check mark.
* `CAL RANGE` reports the stored point count and frequency span for the active calibration. Invoking the entry re-applies those limits (and the recorded power) when the calibration was interpolated.
* `CAL POWER` selects Si5351 drive strength. Choose `AUTO` for adaptive control or one of the explicit currents (2–8 mA).
* `SAVE CAL` lists every calibration slot, annotating each with its stored span if populated. Selecting a slot writes the current coefficients.
* `CAL APPLY` toggles correction without discarding coefficients.
* `ENHANCED RESPONSE` enables or disables the enhanced-response algorithm.
* `LOAD STD` *(with `__VNA_Z_RENORMALIZATION__`)* lets you edit the nominal load impedance used during calibration.
* `CAL RESET` clears the working calibration (enhanced-response state is preserved).

### SAVE/RECALL
This companion submenu mirrors the slot operations for field workflows:
* `SAVE CAL` / `RECALL CAL` provide immediate access to calibration storage without stepping through the MECH CAL flow.
* `CAL APPLY` and `CAL RESET` duplicate the toggles so you can quickly switch correction on/off after recalling a slot.

## STIMULUS menu

* `START`, `STOP`, `CENTER`, `SPAN` — keypad-driven sweep boundaries.
* `CW FREQ` — sets a fixed continuous-wave frequency (the sweep pauses until you resume it manually).
* `FREQ STEP` — adjusts the coarse tuning step used by the jog controls.
* `JOG STEP` — toggles between automatic increments and the value entered via keypad.
* `SET POINTS` — direct keypad entry for arbitrary sweep point counts.
* `%d PTS` buttons — shortcut presets compiled from `POINTS_SET`. Each button shows the resolved point count.
* `MORE PTS` — re-opens the legacy sweep-points submenu if you need additional presets beyond the ones shown inline.

## DISPLAY menu

### Traces and formats
* `TRACES` — toggles `TRACE 0`–`TRACE 3`. Selecting an enabled trace focuses it; disabled traces are turned on. When `STORED_TRACES` is enabled, additional entries allow storing and recalling frozen traces.
* `FORMAT S11` — lists all reflection formats for the active trace: `LOGMAG`, `PHASE`, `DELAY`, `SMITH`, `SWR`, `RESISTANCE`, `REACTANCE`, `|Z|`, with nested `MORE` pages for `POLAR`, `LINEAR`, `REAL`, `IMAG`, `Q FACTOR`, `CONDUCTANCE`, `SUSCEPTANCE`, `|Y|`, `Z PHASE`, `SERIES/SHUNT/ PARALLEL` component views.
* `FORMAT S21` — equivalent menu for transmission formats. The `MORE` branch exposes shunt/series impedance and Q-factor views.
* `CHANNEL` — swaps the focused trace between reflection (CH0) and transmission (CH1).

### Scaling
* `SCALE` — provides `AUTO SCALE`, manual `TOP`, `BOTTOM`, `SCALE/DIV`, `REFERENCE POSITION`, `E-DELAY`, `S21 OFFSET`, and (with `__USE_GRID_VALUES__`) toggles for grid overlays.

### Markers
Markers now live directly under DISPLAY:
* `MARKERS` -> opens the marker control surface.
  * `SELECT MARKER` lists every slot allowed by `MARKERS_MAX`. Active markers show `CHECK` icons; the currently driven marker shows `AUTO` in its icon. Buttons `ALL OFF` and `DELTA` appear below the list.
  * `TRACKING` toggles marker tracking.
  * `SEARCH` displays the current search mode (MAXIMUM/MINIMUM). `SEARCH ← LEFT` and `SEARCH -> RIGHT` jump to the next extremum in each direction.
  * `MOVE START/STOP/CENTER/SPAN` transfer the highlighted marker (or marker pair) into the corresponding sweep parameter.
  * `MARKER E-DELAY` applies the measured delay to the active trace.

## MEASURE menu

This menu consolidates DSP helpers and measurement assistants.

* `TRANSFORM` — toggles time-domain transform, select filter (`LOW PASS IMPULSE`, `LOW PASS STEP`, `BANDPASS`), choose window shape, and edit the velocity factor.
* `DATA SMOOTH` *(with `__USE_SMOOTH__`)* — choose between OFF and the compiled averaging depths. A status button shows the geometry (Arith/Geom) toggle.
* `MEASURE` *(with `__VNA_MEASURE_MODULE__`)* — context-aware measurement modes. The entry opens the specialised submenu tied to the current mode (L/C match, cable length, resonance, S21 fixtures, filter). Each specialised view exposes the parameters described in the firmware (velocity factor, load R, cable length, etc.).
* `IF BANDWIDTH` — lists the synthesiser bandwidth presets compiled for the target board.
* `PORT-Z` *(with `__VNA_Z_RENORMALIZATION__`)* — edits the reference impedance for S11 processing.

## SYSTEM menu

### System utilities
* `TOUCH CAL` / `TOUCH TEST` — touch calibration and verification utilities.
* `BRIGHTNESS` *(with `__LCD_BRIGHTNESS__`)* — adjusts LCD backlight duty cycle.
* `SAVE CONFIG` — forces configuration plus any pending autosave data to flash.
* `VERSION` — displays firmware build metadata.
* `DATE/TIME` *(with `__USE_RTC__`)* — provides `SET DATE`, `SET TIME`, `RTC CAL`, and `RTC 512 Hz / LED2` output control.

### Device (expert settings)
* `DEVICE` -> opens advanced hardware controls:
  * `THRESHOLD`, `TCXO`, `VBAT OFFSET` — keypad-entry system constants.
  * `IF OFFSET` *(with `USE_VARIABLE_OFFSET_MENU`)* — choose from compiled IF offsets.
  * `REMEMBER STATE` *(with `__USE_BACKUP__`)* — enables the autosave daemon that snapshots sweep/UI state shortly after edits.
  * `FLIP DISPLAY` *(with `__FLIP_DISPLAY__`)* — mirror the LCD.
  * `DFU` *(with `__DFU_SOFTWARE_MODE__`)* — soft boots into the DFU ROM (path: System -> Device -> DFU).
  * `MORE` -> exposes manufacturing tools:
    * `MODE` — select the Si5351-compatible synthesiser variant.
    * `SEPARATOR` *(with `__DIGIT_SEPARATOR__`)* — choose decimal/comma formatting.
    * `USB DEVICE UID` *(with `__USB_UID__`)* — toggle use of the MCU unique ID for USB enumeration.
    * `CLEAR CONFIG` -> contains `CLEAR ALL AND RESET`, which wipes flash-stored configuration.
* `CONNECTION` *(with `__USE_SERIAL_CONSOLE__`)* — switch the shell transport between USB CDC and the UART bridge; select the UART bit rate.

## SD CARD menu

* `LOAD` *(with `__SD_FILE_BROWSER__`)* — opens the browser filtered by extension (`SCREENSHOT`, `S1P`, `S2P`, `CAL`) so you can load screenshots, Touchstone files, or calibration sets directly on the instrument.
* `SAVE S1P` / `SAVE S2P` — capture the current sweep into S1P or S2P using calibrated data.
* `SCREENSHOT` — dumps the LCD as BMP or (when `IMAGE FORMAT` is set to `TIF`) as a compact PackBits-compressed TIFF.
* `SAVE CALIBRATION` — copies the active calibration to the SD card for archival or transfer.
* `IMAGE FORMAT` *(with `__SD_CARD_DUMP_TIFF__`)* — toggles the screenshot container between BMP and TIF.
* `FORMAT SD` *(with `FF_USE_MKFS`)* — unmounts the card, runs FatFs `f_mkfs` with `FM_FAT`, remounts, and reports success/failure. Use this when a card was re-partitioned externally or you need a clean Keysight-style single-volume layout.

## Sweep control button

The final button reflects the sweep status: `PAUSE SWEEP` while sweeping, `RESUME SWEEP` when halted. Press it to toggle the background sweep and update the label/icon.

## USB console prompt behaviour

Automation clients rely on the USB shell prompt. Each session starts with `\r\nch> \r\nNanoVNA Shell\r\nch> ` so host software can synchronise on the prompt before parsing the banner.

---

This document mirrors the defaults in `include/nanovna.h` and `include/app.app_features.h`. Disable a feature flag at build time and the related menu entries disappear automatically.
