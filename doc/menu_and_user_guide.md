# NanoVNA-X Menu and User Workflow Reference

This document consolidates the NanoVNA-X touch-screen menu structure and the core operating
workflow for new users. It draws on the interactive menu map curated by Oristopo and the
comprehensive user guide edited by L. Rothman to keep firmware behaviour aligned with
popular NanoVNA builds.

## Top-level menu layout

The firmware exposes the same top-level menu entries documented for classic NanoVNA-H
releases. Each heading below summarises the buttons found on the corresponding screen and
how they behave on NanoVNA-X.

### DISPLAY

* **TRACE** – Toggle individual traces on or off (up to four with compatible firmware) and
  choose which trace is active before adjusting format, scale or channel assignments.
* **FORMAT** – Pick the rendering mode for the active trace: logarithmic magnitude,
  phase, delay, Smith chart, SWR, polar, linear magnitude, real/imaginary Cartesian
  components, resistance or reactance views.
* **SCALE** – Adjust the vertical gain of the active trace via *Scale/Div*, shift it
  vertically with *Reference Position* and apply an *Electrical Delay* compensation in
  picoseconds.
* **CHANNEL** – Switch measurement sources between **CH0 REFLECT** and **CH1 THROUGH**.
* **TRANSFORM** – Enable time-domain conversions (*Transform On*) and choose between
  low-pass impulse/step or band-pass responses, select the window (minimum/normal/maximum)
  and set the cable *Velocity Factor*.

### MARKER

* Reposition sweep bounds to the highlighted marker with *Start* or *Stop*.
* Jump the centre or span of the sweep to the current marker pair with *Center* and
  *Span*.
* Enable or disable up to four markers individually or hide all markers with *All Off*.

### STIMULUS

* Set sweep boundaries using *Start* and *Stop* (or *Center* plus *Span*).
* Configure a continuous-wave spot measurement with *CW Freq*.
* Temporarily freeze acquisition with *Pause Sweep* while you inspect or export data.

### CAL

* Launch the step-by-step calibration assistant through *Calibrate*.
* Execute the standards sequence: **Open**, **Short**, **Load**, **Isolation**, **Thru**,
  followed by *Done* when the trace stabilises.
* Preserve the solved error terms in one of four slots using *Save 0–3*.
* Clear live calibration data with *Reset* and toggle correction on/off with *Correction*.

### RECALL

* Restore a stored calibration/profile from slots *Recall 0–3*.

### CONFIG

* Calibrate or test the touch panel, persist device preferences, inspect firmware version
  details or reboot into the DFU bootloader.

> **Reference**: Interactive menu map for NanoVNA firmware, accessible at
> <https://oristopo.github.io/nVhelp/html/Menu.htm>.

## Input and keypad tips

The on-screen keypad mirrors the behaviour described in the upstream user guide: tap the
numeric keys to enter values, use *Back* to delete the last character, and finish entry with
unit shortcuts that immediately scale the typed number (for example, `M` multiplies by one
million). Older firmware builds may hide the keypad icon, but touching the blank bar in the
lower-right corner still summons the keypad overlay.

## Core measurement workflow

A reliable sweep follows the same simple loop outlined in the NanoVNA user guide:

1. Define the frequency range – set *Start/Stop* or *Center/Span* before connecting the
   device under test.
2. Perform a fresh calibration covering that range and save the results to a slot.
3. Hook up the DUT and capture traces or markers as needed.

## Recommended calibration sequence

Calibrate whenever you change the sweep span or reconnect fixtures. The guide recommends the
following order once the trace settles at each step:

1. Open standard on CH0 → **Open**
2. Short standard on CH0 → **Short**
3. 50 Ω load on CH0 → **Load**
4. 50 Ω loads on CH0 and CH1 → **Isolation** (leave CH0 open if only one load is available)
5. Through connection between CH0 and CH1 → **Thru**
6. Confirm convergence → **Done**, then store in a slot via **Save 0–3**

After loading calibration later, verify the status banner (`Cn DRSTX`) still matches the
intended slot when you adjust sweep limits.

## Additional operating notes

* Use markers to quickly retune the sweep edges to specific resonances or impedance targets.
* The transform menu unlocks TDR-style analysis; experiment with window shapes and velocity
  factor to line up distance estimates with physical cable lengths.
* Pause the sweep before exporting data or repositioning fixtures to avoid transient traces.

> **Reference**: L. Rothman, *NanoVNA User Guide*, rev. Jan 15 2020, available at
> <http://ha3hz.hu/images/download/NanoVNA-User-Guide-English-reformat-Jan-15-20.pdf>.
