#include "processing/calibration.h"
#include <string.h>
#include "nanovna.h"
#include "rf/sweep/sweep_orchestrator.h"

// Forward declarations of static functions
static void eterm_set(int term, float re, float im);
static void eterm_copy(int dst, int src);
static void eterm_calc_es(void);
static void eterm_calc_er(int sign);
static void eterm_calc_et(void);

// need_interpolate is now extern in nanovna.h


static void eterm_set(int term, float re, float im) {
  int i;
  for (i = 0; i < sweep_points; i++) {
    cal_data[term][i][0] = re;
    cal_data[term][i][1] = im;
    
    // Yield periodically to keep UI responsive during intensive computation
    if ((i & 0xF) == 0) {  // yield every 16 iterations
      chThdYield();
    }
  }
}

static void eterm_copy(int dst, int src) {
  memcpy(cal_data[dst], cal_data[src], sizeof cal_data[dst]);
}

static void eterm_calc_es(void) {
  int i;
  for (i = 0; i < sweep_points; i++) {
    // S11mo' = S11mo - ED (after directivity correction)
    // S11ms' = S11ms - ED (after directivity correction)
    float open_r = cal_data[CAL_OPEN][i][0] - cal_data[ETERM_ED][i][0];
    float open_i = cal_data[CAL_OPEN][i][1] - cal_data[ETERM_ED][i][1];
    float short_r = cal_data[CAL_SHORT][i][0] - cal_data[ETERM_ED][i][0];
    float short_i = cal_data[CAL_SHORT][i][1] - cal_data[ETERM_ED][i][1];
    
    // ES = (S_open' + S_short') / (S_open' - S_short') 
    // following original DiSlord formula
    float num_r = open_r + short_r;
    float num_i = open_i + short_i;
    float den_r = open_r - short_r;
    float den_i = open_i - short_i;
    
    // Complex division to calculate source match error
    float denom = den_r * den_r + den_i * den_i;
    if (denom > 1e-20f) {
      cal_data[ETERM_ES][i][0] = (num_r * den_r + num_i * den_i) / denom;
      cal_data[ETERM_ES][i][1] = (num_i * den_r - num_r * den_i) / denom;
    } else {
      cal_data[ETERM_ES][i][0] = 0.0f;
      cal_data[ETERM_ES][i][1] = 0.0f;
    }
    
    // Yield periodically to keep UI responsive during intensive computation
    if ((i & 0xF) == 0) {  // yield every 16 iterations
      chThdYield();
    }
  }
  cal_status &= ~CALSTAT_OPEN;
  cal_status |= CALSTAT_ES;
}

static void eterm_calc_er(int sign) {
  int i;
  for (i = 0; i < sweep_points; i++) {
    // Er = sign*(1-sign*Es)S11ms'
    float s11sr = cal_data[CAL_SHORT][i][0] - cal_data[ETERM_ED][i][0];
    float s11si = cal_data[CAL_SHORT][i][1] - cal_data[ETERM_ED][i][1];
    float esr = cal_data[ETERM_ES][i][0];
    float esi = cal_data[ETERM_ES][i][1];
    if (sign > 0) {
      esr = -esr;
      esi = -esi;
    }
    esr = 1 + esr;
    float err = esr * s11sr - esi * s11si;
    float eri = esr * s11si + esi * s11sr;
    if (sign < 0) {
      err = -err;
      eri = -eri;
    }
    cal_data[ETERM_ER][i][0] = err;
    cal_data[ETERM_ER][i][1] = eri;
    
    // Yield periodically to keep UI responsive during intensive computation
    if ((i & 0xF) == 0) {  // yield every 16 iterations
      chThdYield();
    }
  }
  cal_status &= ~CALSTAT_SHORT;
  cal_status |= CALSTAT_ER;
}

// CAUTION: Et is inversed for efficiency
static void eterm_calc_et(void) {
  int i;
  for (i = 0; i < sweep_points; i++) {
    // Et = 1/(S21mt - Ex)
    float etr = cal_data[CAL_THRU][i][0] - cal_data[CAL_ISOLN][i][0];
    float eti = cal_data[CAL_THRU][i][1] - cal_data[CAL_ISOLN][i][1];
    float sq = etr * etr + eti * eti;
    if (sq > 1e-20f) {
      float invr = etr / sq;
      float invi = -eti / sq;
      cal_data[ETERM_ET][i][0] = invr;
      cal_data[ETERM_ET][i][1] = invi;
    } else {
      // Set to default value if division by zero would occur
      cal_data[ETERM_ET][i][0] = 1.0f;
      cal_data[ETERM_ET][i][1] = 0.0f;
    }
    
    // Yield periodically to keep UI responsive during intensive computation
    if ((i & 0xF) == 0) {  // yield every 16 iterations
      chThdYield();
    }
  }
  cal_status &= ~CALSTAT_THRU;
  cal_status |= CALSTAT_ET;
}

void cal_collect(uint16_t type) {
  uint16_t dst, src;

  static const struct {
    uint16_t set_flag;
    uint16_t clr_flag;
    uint8_t dst;
    uint8_t src;
  } calibration_set[] = {
      //    type       set data flag                              reset flag  destination source
      [CAL_LOAD] = {CALSTAT_LOAD, ~(CALSTAT_APPLY), CAL_LOAD, 0},
      [CAL_OPEN] = {CALSTAT_OPEN, ~(CALSTAT_ES | CALSTAT_ER | CALSTAT_APPLY), CAL_OPEN,
                    0}, // Reset Es and Er state
      [CAL_SHORT] = {CALSTAT_SHORT, ~(CALSTAT_ES | CALSTAT_ER | CALSTAT_APPLY), CAL_SHORT,
                     0}, // Reset Es and Er state
      [CAL_THRU] = {CALSTAT_THRU, ~(CALSTAT_ET | CALSTAT_APPLY), CAL_THRU, 1}, // Reset Et state
      [CAL_ISOLN] = {CALSTAT_ISOLN, ~(CALSTAT_APPLY), CAL_ISOLN, 1},
  };
  if (type >= ARRAY_COUNT(calibration_set))
    return;

  // reset old calibration if frequency range/points not some
  freq_t cal_start, cal_stop;
  freq_t a = frequency0;
  freq_t b = frequency1;
  if (a <= b) {
    cal_start = a;
    cal_stop = b;
  } else {
    cal_start = b;
    cal_stop = a;
  }
  
  if (need_interpolate(cal_start, cal_stop, sweep_points)) {
    cal_status = 0;
    cal_frequency0 = cal_start;
    cal_frequency1 = cal_stop;
    cal_sweep_points = sweep_points;
  }
  cal_power = current_props._power;

  cal_status &= calibration_set[type].clr_flag;
  cal_status |= calibration_set[type].set_flag;
  dst = calibration_set[type].dst;
  src = calibration_set[type].src;

  // Run sweep for collect data (use minimum BANDWIDTH_30, or bigger if set)
  uint8_t bw = config._bandwidth; // store current setting
  if (bw < BANDWIDTH_100)
    config._bandwidth = BANDWIDTH_100;

  uint16_t mask = (src == 0) ? SWEEP_CH0_MEASURE : SWEEP_CH1_MEASURE;
  
  // Measure calibration data
  app_measurement_sweep(false, mask);
  
  // Mark that we're about to modify calibration data - temporarily set flag during critical copy
  calibration_in_progress = true;
  
  // Copy calibration data - this is critical section that should not be interrupted
  memcpy(cal_data[dst], measured[src], sizeof measured[0]);

  // Made average if need - also in critical section to maintain data consistency
  int count = 1, i, j;
  for (i = 1; i < count; i++) {
    app_measurement_sweep(false, (src == 0) ? SWEEP_CH0_MEASURE : SWEEP_CH1_MEASURE);
    for (j = 0; j < sweep_points; j++) {
      cal_data[dst][j][0] += measured[src][j][0];
      cal_data[dst][j][1] += measured[src][j][1];
    }
  }
  if (i != 1) {
    float k = 1.0f / i;
    for (j = 0; j < sweep_points; j++) {
      cal_data[dst][j][0] *= k;
      cal_data[dst][j][1] *= k;
    }
  }
  
  // Clear the flag - calibration data collection complete for this specific measurement
  calibration_in_progress = false;
  
  config._bandwidth = bw; // restore
  request_to_redraw(REDRAW_CAL_STATUS);
}

void cal_done(void) {
  // Indicate that calibration critical processing is starting
  calibration_in_progress = true;
  
  // Set Load/Ed to default if not calculated
  if (!(cal_status & CALSTAT_LOAD))
    eterm_set(ETERM_ED, 0.0, 0.0);
  // Set Isoln/Ex to default if not measured
  if (!(cal_status & CALSTAT_ISOLN))
    eterm_set(ETERM_EX, 0.0, 0.0);

  // Precalculate Es and Er from Short and Open (and use Load/Ed data)
  if ((cal_status & CALSTAT_SHORT) && (cal_status & CALSTAT_OPEN)) {
    eterm_calc_es();
    eterm_calc_er(-1);
  } else if (cal_status & CALSTAT_OPEN) {
    eterm_copy(CAL_SHORT, CAL_OPEN);
    cal_status &= ~CALSTAT_OPEN;
    eterm_set(ETERM_ES, 0.0, 0.0);
    eterm_calc_er(1);
  } else if (cal_status & CALSTAT_SHORT) {
    eterm_set(ETERM_ES, 0.0, 0.0);
    eterm_calc_er(-1);
  }

  // Apply Et
  if (cal_status & CALSTAT_THRU)
    eterm_calc_et();

  // Set other fields to default if not set
  if (!(cal_status & CALSTAT_ET))
    eterm_set(ETERM_ET, 1.0, 0.0);
  if (!(cal_status & CALSTAT_ER))
    eterm_set(ETERM_ER, 1.0, 0.0);
  if (!(cal_status & CALSTAT_ES))
    eterm_set(ETERM_ES, 0.0, 0.0);

  cal_status |= CALSTAT_APPLY;
  lastsaveid = NO_SAVE_SLOT;
  request_to_redraw(REDRAW_BACKUP | REDRAW_CAL_STATUS);
  
  // Indicate that calibration processing is complete
  calibration_in_progress = false;
}
