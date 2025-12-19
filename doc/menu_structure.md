# NanoVNA-X Menu Structure

## CAL

- **MECH CAL**
  - OPEN -> apply standard
  - SHORT
  - LOAD
  - ISOLN
  - THRU
  - DONE -> store to flash
  - DONE IN RAM -> apply without persistence
- CAL RANGE -> show/apply stored span & points
- CAL POWER -> AUTO / 2 mA / 4 mA / 6 mA / 8 mA
- SAVE CAL -> select slot and store coefficients
- CAL APPLY -> toggle correction on/off
- ENHANCED RESPONSE -> toggle enhanced-response correction
- LOAD STD `[__VNA_Z_RENORMALIZATION__]` -> set standard load impedance
- CAL RESET -> clear the working calibration
- **SAVE/RECALL**
  - SAVE CAL -> write active coefficients
  - RECALL CAL -> load stored span
  - CAL APPLY
  - CAL RESET

For detailed 12-point calibration instructions, see the documentation:
- [12-Point Calibration Guide](calibration_12_point.md)

## STIMULUS

- START / STOP / CENTER / SPAN -> keypad entries
- CW FREQ -> single-frequency output
- FREQ STEP -> jog coarse step
- JOG STEP -> AUTO or manual setting
- SET POINTS -> keypad entry
- `%d PTS` buttons -> presets compiled from `POINTS_SET`
- MORE PTS -> legacy sweep-points submenu

## DISPLAY

- TRACES -> TRACE 0 … TRACE 3, stored trace slots `[STORED_TRACES > 0]`
- FORMAT S11 -> LOGMAG, PHASE, DELAY, SMITH, SWR, RESISTANCE, REACTANCE, |Z|, MORE -> (POLAR, LINEAR, REAL, IMAG, Q FACTOR, CONDUCTANCE, SUSCEPTANCE, |Y|) -> MORE -> (Z PHASE, SERIES/SHUNT/PARALLEL component views)
- FORMAT S21 -> LOGMAG, PHASE, DELAY, SMITH, POLAR, LINEAR, REAL, IMAG, MORE -> (series/shunt R/X/|Z|, Q)
- CHANNEL -> toggle CH0 / CH1 for active trace
- SCALE -> AUTO SCALE, TOP, BOTTOM, SCALE/DIV, REFERENCE POSITION, E-DELAY, S21 OFFSET, SHOW GRID `[__USE_GRID_VALUES__]`, DOT GRID `[__USE_GRID_VALUES__]`
- MARKERS
  - SELECT MARKER -> MARKER 1 … MARKER N, ALL OFF, DELTA
  - SEARCH
    - SEARCH (MAXIMUM/MINIMUM)
    - TRACKING
    - SEARCH ← LEFT
    - SEARCH -> RIGHT
  - OPERATIONS
    - MOVE START / MOVE STOP / MOVE CENTER / MOVE SPAN
    - MARKER E-DELAY
  - DELTA

## MEASURE

- TRANSFORM -> toggle domain, select LOW PASS IMPULSE / LOW PASS STEP / BANDPASS, change window, VELOCITY FACTOR
- DATA SMOOTH `[__USE_SMOOTH__]` -> OFF, x1/x2/x4/x5/x6…, SMOOTH MODE toggle
- MEASURE `[__VNA_MEASURE_MODULE__]`
  - OFF, L/C MATCH, CABLE (S11), RESONANCE (S11), SHUNT LC (S21), SERIES LC (S21), SERIES XTAL (S21), FILTER (S21)
  - Specialised submenus expose VELOCITY FACTOR, CABLE LENGTH, load R, etc.
- IF BANDWIDTH -> available synthesiser bandwidth presets (e.g., 8 kHz … 10 Hz)
- PORT-Z `[__VNA_Z_RENORMALIZATION__]` -> set reference impedance

## SYSTEM

- TOUCH CAL
- TOUCH TEST
- BRIGHTNESS `[__LCD_BRIGHTNESS__]`
- SAVE CONFIG
- VERSION
- DATE/TIME `[__USE_RTC__]` -> SET DATE, SET TIME, RTC CAL, RTC 512 Hz/LED2
- **DEVICE**
  - THRESHOLD
  - TCXO
  - VBAT OFFSET
  - IF OFFSET `[USE_VARIABLE_OFFSET_MENU]`
  - REMEMBER STATE `[__USE_BACKUP__]`
  - FLIP DISPLAY `[__FLIP_DISPLAY__]`
  - DFU `[__DFU_SOFTWARE_MODE__]`
  - MORE
    - MODE (Si5351 / MS5351 / SWC5351 selection)
    - SEPARATOR `[__DIGIT_SEPARATOR__]`
    - USB DEVICE UID `[__USB_UID__]`
    - CLEAR CONFIG -> CLEAR ALL AND RESET
- CONNECTION `[__USE_SERIAL_CONSOLE__]` -> toggle USB/UART, set serial speed

## SD CARD

- LOAD `[__SD_FILE_BROWSER__]` -> open browser filtered by extension (SCREENSHOT, S1P, S2P, CAL)
- SAVE S1P -> dump CH0 data to an S1P file
- SAVE S2P -> dump both channels to an S2P file
- SCREENSHOT -> capture the active LCD image (BMP or TIFF depending on the IMAGE FORMAT toggle)
- SAVE CALIBRATION -> write the current calibration set to the card
- IMAGE FORMAT `[__SD_CARD_DUMP_TIFF__]` -> toggles between `BMP` and `TIF`
- FORMAT SD `[FF_USE_MKFS]` -> unmount, run FatFs mkfs (`FM_FAT`), remount, and report the outcome

## Sweep control

- `%s SWEEP` button reflects the sweep state (`PAUSE SWEEP` or `RESUME SWEEP`) and immediately toggles the background sweep engine.
