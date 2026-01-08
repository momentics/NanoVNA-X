# 12-Point Calibration for NanoVNA-X

## General Information

12-point calibration is an extended form of SOLT (Short-Open-Load-Through) calibration that includes an additional parameter Isolation (ISOLN), resulting in 12 error coefficients. This methodology allows compensation for errors caused by the non-ideal nature of the measurement system and provides more accurate S-parameter measurement results.

* [12-Point Calibration Guide (English)](calibration_12_point.md)
* [12-точечная калибровка (Russian)](calibration_12_point_RU.md)

## Preparation for Calibration

1. **Prepare calibration standard elements**:
   * Short circuit calibration standard (SHORT)
   * Open circuit calibration standard (OPEN)  
   * Load calibration standard (LOAD, typically 50 Ohms)
   * Through calibration standard (THRU, 50 Ohm connector)
   * Connecting cables for connecting standards

> **Note on Calibration Standards**: The ultimate measurement accuracy of the NanoVNA-X is intrinsically linked to the quality of the calibration standards employed, with the **LOAD** standard being particularly critical. It is important to recognize that many low-cost commercial terminations often exhibit non-negligible impedance deviations and parasitic reactance as frequency increases. Since the calibration process establishes the "truth" of 50 Ohms based on this component, any imperfections in the LOAD will directly translate into measurement errors across the entire dynamic range. For high-fidelity results, we strongly recommend using a precision-grade load with verified return loss characteristics across the intended frequency band.

2. **Check the condition of standards**:
   * Ensure connectors are clean and undamaged
   * Check for visible contamination or oxidation
   * Verify secure connections

3. **Power on the instrument** and wait for full initialization

4. **Set the required frequency range**:
   * Enter the sweep settings menu
   * Set start and stop frequencies according to your requirements
   * Set the number of measurement points (typically 101, 201, 401, etc.)

## Calibration SOLT (Order in menu: OPEN-SHORT-LOAD-ISOLN-THRU)

### Step 1: OPEN Calibration

1. Ensure nothing is connected to port CH0 (OPEN)
2. Go to menu: `CAL` -> `CAL WIZARD` -> `OPEN`
3. Wait for the standard measurement to complete
4. Verify that the "O" indicator in the status bar is active

### Step 2: SHORT Calibration

1. Connect the SHORT standard to port CH0
2. Go to menu: `CAL` -> `CAL WIZARD` -> `SHORT`
3. Wait for the standard measurement to complete
4. Verify that the "S" indicator in the status bar is active

### Step 3: LOAD Calibration

1. Connect the LOAD standard to port CH0 (or CH1 depending on measurement)
2. Go to menu: `CAL` -> `CAL WIZARD` -> `LOAD`
3. Wait for the standard measurement to complete
4. Verify that the "L" indicator in the status bar is active (checked with a tick)

### Step 4: ISOLN Calibration (Isolation)

1. Ensure nothing is connected to both ports CH0 and CH1 (OPEN both ports)
2. Go to menu: `CAL` -> `CAL WIZARD` -> `ISOLN`
3. Wait for the background noise/leakage measurement to complete
4. Verify that the "X" indicator in the status bar is active

Important! ISOLN measures cross-coupling between ports and background noise, which is especially important for accurate S21 measurements.

### Step 5: THRU Calibration

1. Connect the THRU standard between ports CH0 and CH1
2. Go to menu: `CAL` -> `CAL WIZARD` -> `THRU`
3. Wait for the standard measurement to complete
4. Verify that the "T" indicator in the status bar is active

## Finishing Calibration

### Step 6: Finish and Apply Calibration

1. After completing all standard measurements, select:
   * `CAL` -> `DONE` to save the calibration and apply it to all subsequent measurements
   * OR `CAL` -> `DONE IN RAM` to apply the calibration without saving to non-volatile memory

2. Verify that the "CAL" indicator in the status bar is active, indicating that calibration is enabled

3. Check the calibration correctness:
   * Connect the load standard again and verify that S11 is close to 0∠0°
   * Connect the short circuit and verify that S11 is close to -1∠0°
   * Connect the open circuit and verify that S11 is close to +1∠0°

## Save and Recall Calibration

### Save Calibration

1. Go to menu: `SAVE CAL`
2. Select a free slot for saving
3. Calibration will be saved in non-volatile memory

### Recall Calibration

1. Go to menu: `RECALL CAL`
2. Select the required slot with saved calibration
3. The system will load the calibration coefficients

## Recommendations and Tips

1. **Temperature Effects**: Calibration can change with temperature, so for high-precision measurements, perform calibration at a temperature close to the measurement temperature.

2. **Connection Sequence**: Connect standards in the same physical position and with the same connecting cables that you will use for measuring DUT (Device Under Test).

3. **Stability Check**: Periodically check calibration using a known standard to ensure its stability.

4. **Save Multiple Calibrations**: Save different calibrations for different frequency ranges and measurement configurations.

5. **Recalibration Frequency**: Before important measurements or when changing frequency range, number of points, or other measurement parameters, it is recommended to perform a new calibration.

## Troubleshooting

* If calibration doesn't save: check if memory isn't full
* If measurements are incorrect: ensure all standards are measured correctly (check indicators in status bar)
* If graphs look incorrect: try disabling and re-enabling calibration through `CAL` -> `CAL ON/OFF`

## Conclusion

Proper 12-point calibration provides maximum accuracy for S-parameter measurements. Following this instruction will allow you to perform reliable calibration of your NanoVNA-X and obtain accurate measurement results.
