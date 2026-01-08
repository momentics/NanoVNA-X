# 50 Ohm and 75 Ohm Support (PORT-Z)

NanoVNA-X supports measurements in systems with different characteristic impedances (e.g., 75 Ohm for TV and video equipment) while maintaining the standard 50 Ohm calibration.

## How it works

The hardware is designed for 50 Ohm impedance. All physical measurements are performed in a 50 Ohm environment.
To work with a 75 Ohm load, a mathematical **impedance renormalization** function is used.

1. Perform a standard SOLT calibration using a 50 Ohm calibration kit (Open, Short, Load 50 Ohm).
    * **Important:** Always perform calibration with a **50 Ohm calibration kit**.
    * Do not use a 75 Ohm load for calibration. The firmware assumes a 50 Ohm standard for the calibration process.
    * The renormalization feature (`PORT-Z`) mathematically converts the 50 Ohm calibrated data to 75 Ohm.

2. Change the port impedance (`PORT-Z`) to 75 Ohm in the device menu.
    * The device recalculates the measured S-parameters (Gamma) as if the measurement was taken with 75 Ohm ports.
    * The center of the Smith Chart shifts to the 75 Ohm point.
    * VSWR is recalculated relative to 75 Ohm.

## Instruction: Setting up 75 Ohm

1. Perform a standard calibration with 50 Ohm loads.
2. Navigate to the menu: `MEASURE`.
3. Locate the `PORT-Z` item. It defaults to `50 Ohm`.
4. Tap `PORT-Z`, enter `75`, and press `x1`.
5. All plots (Smith, LogMag, SWR) will now display values renormalized for a 75 Ohm system.

## Physical Limitations

* Using 50 Ohm connectors and cables introduces a small mismatch error. The renormalization mathematics attempts to compensate for the impedance change, but physical reflections at 50/75 Ohm interfaces remain.
* For precise laboratory measurements, it is recommended to use 50-75 Ohm Matching Pads (Minimum Loss Pads). However, these introduce attenuation (approximately 5.7 dB), which must be compensated for in the calibration or by using S21 offset.
* The software `PORT-Z` method is ideal for quick evaluation of 75 Ohm antennas and cables without additional hardware.
