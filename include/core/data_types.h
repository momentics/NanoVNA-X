#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include "core/config_macros.h"

// Define frequency range (can be unsigned)
typedef uint32_t freq_t;

#ifndef MAX_FREQ_TYPE
#define MAX_FREQ_TYPE 5
enum stimulus_type {
  ST_START=0, ST_STOP, ST_CENTER, ST_CW, ST_SPAN, ST_STEP, ST_VAR
};
#endif

// Max palette indexes in config
#define MAX_PALETTE     32

// trace 
#define MAX_TRACE_TYPE 30
enum trace_type {
  TRC_LOGMAG=0, TRC_PHASE, TRC_DELAY, TRC_SMITH, TRC_POLAR, TRC_LINEAR, TRC_SWR, TRC_REAL, TRC_IMAG,
  TRC_R, TRC_X, TRC_Z, TRC_ZPHASE,
  TRC_G, TRC_B, TRC_Y, TRC_Rp, TRC_Xp,
  TRC_Cs, TRC_Ls,
  TRC_Cp, TRC_Lp,
  TRC_Q,
  TRC_Rser, TRC_Xser, TRC_Zser,
  TRC_Rsh, TRC_Xsh, TRC_Zsh,
  TRC_Qs21
};

// Mask for define rectangular plot
#define RECTANGULAR_GRID_MASK ((1<<TRC_LOGMAG)|(1<<TRC_PHASE)|(1<<TRC_DELAY)|(1<<TRC_LINEAR)|(1<<TRC_SWR)|(1<<TRC_REAL)|(1<<TRC_IMAG)\
                              |(1<<TRC_R)|(1<<TRC_X)|(1<<TRC_Z)|(1<<TRC_ZPHASE)\
                              |(1<<TRC_G)|(1<<TRC_B)|(1<<TRC_Y)|(1<<TRC_Rp)|(1<<TRC_Xp)\
                              |(1<<TRC_Cs)|(1<<TRC_Ls)\
                              |(1<<TRC_Cp)|(1<<TRC_Lp)\
                              |(1<<TRC_Q)\
                              |(1<<TRC_Rser)|(1<<TRC_Xser)|(1<<TRC_Zser)\
                              |(1<<TRC_Rsh)|(1<<TRC_Xsh)|(1<<TRC_Zsh)\
                              |(1<<TRC_Qs21))

// complex graph type (polar / smith / admit)
#define ROUND_GRID_MASK       ((1<<TRC_POLAR)|(1<<TRC_SMITH))
// Scale / Amplitude input in nano/pico values graph type
#define NANO_TYPE_MASK        ((1<<TRC_DELAY)|(1<<TRC_Cs)|(1<<TRC_Ls)|(1<<TRC_Cp)|(1<<TRC_Lp))
// Universal trace type for both channels
#define S11_AND_S21_TYPE_MASK ((1<<TRC_LOGMAG)|(1<<TRC_PHASE)|(1<<TRC_DELAY)|(1<<TRC_LINEAR)|(1<<TRC_REAL)|(1<<TRC_IMAG)|(1<<TRC_POLAR)|(1<<TRC_SMITH))

// Trace info description structure
typedef float (*get_value_cb_t)(int idx, const float *v); // get value callback
typedef struct trace_info {
  const char *name;            // Trace name
  const char *format;          // trace value printf format for marker output
  const char *dformat;         // delta value printf format
  const char *symbol;          // value symbol
  float refpos;                // default refpos
  float scale_unit;            // default scale
  get_value_cb_t get_value_cb; // get value callback (can be NULL, in this case need add custom calculations)
} trace_info_t;

// marker smith value format
enum {MS_LIN, MS_LOG, MS_REIM, MS_RX, MS_RLC, MS_GB, MS_GLC, MS_RpXp, MS_RpLC, MS_SHUNT_RX, MS_SHUNT_RLC, MS_SERIES_RX, MS_SERIES_RLC, MS_END};
#define ADMIT_MARKER_VALUE(v)    ((1<<(v))&((1<<MS_GB)|(1<<MS_GLC)|(1<<MS_RpXp)|(1<<MS_RpLC)))
#define LC_MARKER_VALUE(v)       ((1<<(v))&((1<<MS_RLC)|(1<<MS_GLC)|(1<<MS_RpLC)|(1<<MS_SHUNT_RLC)|(1<<MS_SERIES_RLC)))

#define S11_SMITH_VALUE(v)       ((1<<(v))&((1<<MS_LIN)|(1<<MS_LOG)|(1<<MS_REIM)|(1<<MS_RX)|(1<<MS_RLC)|(1<<MS_GB)|(1<<MS_GLC)|(1<<MS_RpXp)|(1<<MS_RpLC)))
#define S21_SMITH_VALUE(v)       ((1<<(v))&((1<<MS_LIN)|(1<<MS_LOG)|(1<<MS_REIM)|(1<<MS_SHUNT_RX)|(1<<MS_SHUNT_RLC)|(1<<MS_SERIES_RX)|(1<<MS_SERIES_RLC)))

typedef struct {
  const char *name;         // Trace name
  const char *format;       // trace value printf format for marker output
  get_value_cb_t get_re_cb; // get real value callback
  get_value_cb_t get_im_cb; // get imag value callback (can be NULL, in this case need add custom calculations)
} marker_info_t;

// lever_mode
enum {LM_MARKER, LM_SEARCH, LM_FREQ_0, LM_FREQ_1, LM_EDELAY};

#define MARKER_INVALID       -1
#define TRACE_INVALID        -1

// properties flags
#define DOMAIN_MODE             (1<<0)
#define DOMAIN_FREQ             (0<<0)
#define DOMAIN_TIME             (1<<0)
// Time domain function
#define TD_FUNC                 (0b11<<1)
#define TD_FUNC_BANDPASS        (0b00<<1)
#define TD_FUNC_LOWPASS_IMPULSE (0b01<<1)
#define TD_FUNC_LOWPASS_STEP    (0b10<<1)
// Time domain window
#define TD_WINDOW               (0b11<<3)
#define TD_WINDOW_NORMAL        (0b00<<3)
#define TD_WINDOW_MINIMUM       (0b01<<3)
#define TD_WINDOW_MAXIMUM       (0b10<<3)
// Sweep mode
#define TD_START_STOP           (0<<0)
#define TD_CENTER_SPAN          (1<<6)
// Marker track
#define TD_MARKER_TRACK         (1<<7)
// Marker delta
#define TD_MARKER_DELTA         (1<<8)

//
// config.vna_mode flags (16 bit field)
//
enum {
  VNA_MODE_AUTO_NAME = 0,// Auto name for files
#ifdef __USE_SMOOTH__
  VNA_MODE_SMOOTH,       // Smooth function (0: Geom, 1: Arith)
#endif
#ifdef __USE_SERIAL_CONSOLE__
  VNA_MODE_CONNECTION,   // Connection flag (0: USB, 1: SERIAL)
#endif
  VNA_MODE_SEARCH,       // Marker search mode (0: max, 1: min)
  VNA_MODE_SHOW_GRID,    // Show grid values
  VNA_MODE_DOT_GRID,     // Dotted grid lines
#ifdef __USE_BACKUP__
  VNA_MODE_BACKUP,       // Made backup settings (save some settings after power off)
#endif
#ifdef __FLIP_DISPLAY__
  VNA_MODE_FLIP_DISPLAY, // Flip display
#endif
#ifdef __DIGIT_SEPARATOR__
  VNA_MODE_SEPARATOR,    // Comma or dot digit separator (0: dot, 1: comma)
#endif
#ifdef __SD_CARD_DUMP_TIFF__
  VNA_MODE_TIFF,         // Save screenshot format (0: bmp, 1: tiff)
#endif
#ifdef __USB_UID__
  VNA_MODE_USB_UID       // Use unique serial string for USB
#endif
};

// Update config._vna_mode flags function
typedef enum {VNA_MODE_CLR = 0, VNA_MODE_SET, VNA_MODE_TOGGLE} vna_mode_ops;

#ifdef __VNA_MEASURE_MODULE__
// Measure option mode
enum {
  MEASURE_NONE = 0,
#ifdef __USE_LC_MATCHING__
  MEASURE_LC_MATH,
#endif
#ifdef __S21_MEASURE__
  MEASURE_SHUNT_LC,
  MEASURE_SERIES_LC,
  MEASURE_SERIES_XTAL,
  MEASURE_FILTER,
#endif
#ifdef __S11_CABLE_MEASURE__
  MEASURE_S11_CABLE,
#endif
#ifdef __S11_RESONANCE_MEASURE__
  MEASURE_S11_RESONANCE,
#endif
  MEASURE_END
};
#endif

#define STORED_TRACES  1
#define TRACES_MAX     4

typedef struct trace {
  uint8_t enabled;
  uint8_t type;
  uint8_t channel;
  uint8_t smith_format;
  float scale;
  float refpos;
} trace_t;

// marker 1 to 8
#define MARKERS_MAX 8
typedef struct marker {
  uint8_t  enabled;
  uint8_t  reserved;
  uint16_t index;
  freq_t   frequency;
} marker_t;

typedef struct config {
  uint32_t magic;
  uint32_t _harmonic_freq_threshold;
  int32_t  _IF_freq;
  int16_t  _touch_cal[4];
  uint16_t _vna_mode;
  uint16_t _dac_value;
  uint16_t _vbat_offset;
  uint16_t _bandwidth;
  uint8_t  _lever_mode;
  uint8_t  _brightness;
  uint16_t _lcd_palette[MAX_PALETTE];
  uint32_t _serial_speed;
  uint32_t _xtal_freq;
  float    _measure_r;
  uint8_t  _band_mode;
  uint8_t  _reserved[3];
  uint32_t checksum;
} config_t;

#define CAL_TYPE_COUNT  5
#define CAL_LOAD        0
#define CAL_OPEN        1
#define CAL_SHORT       2
#define CAL_THRU        3
#define CAL_ISOLN       4

#define CALSTAT_LOAD (1<<0)
#define CALSTAT_OPEN (1<<1)
#define CALSTAT_SHORT (1<<2)
#define CALSTAT_THRU (1<<3)
#define CALSTAT_ISOLN (1<<4)
#define CALSTAT_ES (1<<5)
#define CALSTAT_ER (1<<6)
#define CALSTAT_ET (1<<7)
#define CALSTAT_ED CALSTAT_LOAD
#define CALSTAT_EX CALSTAT_ISOLN
#define CALSTAT_APPLY (1<<8)
#define CALSTAT_INTERPOLATED (1<<9)
#define CALSTAT_ENHANCED_RESPONSE (1<<10)

#define ETERM_ED 0 /* error term directivity */
#define ETERM_ES 1 /* error term source match */
#define ETERM_ER 2 /* error term refrection tracking */
#define ETERM_ET 3 /* error term transmission tracking */
#define ETERM_EX 4 /* error term isolation */

typedef struct properties {
  uint32_t magic;
  freq_t   _frequency0;          // sweep start frequency
  freq_t   _frequency1;          // sweep stop frequency
  freq_t   _cal_frequency0;      // calibration start frequency
  freq_t   _cal_frequency1;      // calibration stop frequency
  freq_t   _var_freq;            // frequency step by leveler (0 for auto)
  uint16_t _mode;                // timed domain option flag and some others flags
  uint16_t _sweep_points;        // points used in measure sweep
  int8_t   _current_trace;       // 0..(TRACES_MAX -1) (TRACE_INVALID  for disabled)
  int8_t   _active_marker;       // 0..(MARKERS_MAX-1) (MARKER_INVALID for disabled)
  int8_t   _previous_marker;     // 0..(MARKERS_MAX-1) (MARKER_INVALID for disabled)
  uint8_t  _power;               // 0 ... 3 current output power settings
  uint8_t  _cal_power;           // 0 ... 3 Power used in calibration
  uint8_t  _measure;             // additional trace data calculations
  uint16_t _cal_sweep_points;    // points used in calibration
  uint16_t _cal_status;          // calibration data collected flags
  trace_t  _trace[TRACES_MAX];
  marker_t _markers[MARKERS_MAX];
  uint8_t  _reserved;
  uint8_t  _velocity_factor;     // 0 .. 100 %
  float    _electrical_delay[2]; // delays for S11 and S21 traces in seconds
  float    _var_delay;           // electrical delay step by leveler
  float    _s21_offset;          // additional external attenuator for S21 measures
  float    _portz;               // Used for port-z renormalization
  float    _cal_load_r;          // Used as calibration standard LOAD R value (calculated in renormalization procedure)
  uint32_t _reserved1[5];
  float    _cal_data[CAL_TYPE_COUNT][SWEEP_POINTS_MAX][2]; // Put at the end for faster access to others data from struct
  uint32_t checksum;
} properties_t;

// State flags for remote touch state
#define REMOTE_NONE     0
#define REMOTE_PRESS    1
#define REMOTE_RELEASE  2
typedef struct {
  char new_str[6];
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
} remote_region_t;
