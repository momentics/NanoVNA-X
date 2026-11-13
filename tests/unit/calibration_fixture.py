"""Calibration fixtures contributed by @jebcampbellhm in PR #13."""
HEADER = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define NANOVNA_F303 1
#define FLASH_PAGESIZE 2048
#define FLASH_START_ADDRESS 0x08000000u
#define FLASH_TOTAL_SIZE 262144
#define SWEEP_POINTS_MAX 401
#define CAL_TYPE_COUNT 5
#define CAL_LOAD 0
#define CAL_OPEN 1
#define CAL_SHORT 2
#define CAL_THRU 3
#define CAL_ISOLN 4
#define ETERM_ED 0
#define ETERM_ES 1
#define ETERM_ER 2
#define ETERM_ET 3
#define ETERM_EX 4
#define CALSTAT_LOAD 1
#define CALSTAT_OPEN 2
#define CALSTAT_SHORT 4
#define CALSTAT_THRU 8
#define CALSTAT_ISOLN 16
#define CALSTAT_ED CALSTAT_LOAD
#define CALSTAT_EX CALSTAT_ISOLN
#define CALSTAT_ES 32
#define CALSTAT_ER 64
#define CALSTAT_ET 128
#define CALSTAT_APPLY 256
#define CALSTAT_INTERPOLATED 512
#define CALSTAT_ENHANCED_RESPONSE 1024
#define SWEEP_CH0_MEASURE 1
#define SWEEP_CH1_MEASURE 2
#define SWEEP_ENABLE 1
#define SWEEP_ONCE 2
#define BANDWIDTH_100 10
#define REDRAW_BACKUP 1
#define REDRAW_CAL_STATUS 2
#define SAVEAREA_MAX 7
#define SAVE_PROP_CONFIG_SIZE 16384
#define SAVE_CONFIG_SIZE 2048
#define SAVE_PROP_CONFIG_ADDR 0x0801c800u
#define SAVE_CONFIG_ADDR 0x08038800u
#define SAVE_FULL_AREA_SIZE (7*16384+2048)
#define NO_SAVE_SLOT 255
#define PROPERTIES_MAGIC 0x434f4e54u
#define CONFIG_MAGIC 0x12345678u
#define ARRAY_COUNT(a) (sizeof(a)/sizeof(a[0]))
#define MSG_OK 0
#define MS2ST(x) (x)
typedef uint32_t freq_t;
typedef int semaphore_t;
typedef int msg_t;
#define FREQUENCY_MIN 50000u
#define FREQUENCY_MAX 2700000000u
#define TRACES_MAX 4
#define MARKERS_MAX 4
#define MAX_TRACE_TYPE 30
#define MS_END 13
#define LM_EDELAY 4
typedef struct { unsigned type,channel,smith_format; float scale,refpos; } trace_t;
typedef struct { uint16_t index; } marker_t;
typedef struct { uint32_t magic; freq_t _frequency0,_frequency1,_cal_frequency0,_cal_frequency1;
  uint16_t _sweep_points,_cal_sweep_points,_cal_status;
  uint8_t _power,_cal_power; uint32_t _reserved1[5]; float _cal_load_r,_portz;
  int8_t _current_trace,_active_marker,_previous_marker;
  trace_t _trace[TRACES_MAX]; marker_t _markers[MARKERS_MAX];
  float _cal_data[5][401][2]; uint32_t checksum;
} properties_t;
typedef struct { uint32_t magic,_harmonic_freq_threshold; int32_t _IF_freq;
  uint32_t _xtal_freq; uint16_t _bandwidth; uint8_t _band_mode,pad,_lever_mode,_brightness; uint32_t checksum;
} config_t;
extern properties_t current_props;
extern config_t config;
extern uint16_t lastsaveid;
extern uint8_t sweep_mode;
extern bool calibration_in_progress;
extern float measured[2][401][2];
#define frequency0 current_props._frequency0
#define frequency1 current_props._frequency1
#define cal_frequency0 current_props._cal_frequency0
#define cal_frequency1 current_props._cal_frequency1
#define sweep_points current_props._sweep_points
#define cal_sweep_points current_props._cal_sweep_points
#define cal_status current_props._cal_status
#define cal_power current_props._cal_power
#define cal_data current_props._cal_data
#define cal_load_r current_props._cal_load_r
static inline uint32_t __ROR(uint32_t v,unsigned n) { return (v>>n)|(v<<(32-n)); }
void chThdYield(void);
void chSemObjectInit(semaphore_t *,int);
msg_t chSemWaitTimeout(semaphore_t *,unsigned);
void chSemSignal(semaphore_t *);
void flash_program_half_word_buffer(uint16_t *,uint16_t *,uint16_t);
void flash_erase_pages(uint32_t,unsigned);
int config_save(void);
int caldata_save(uint32_t);
int caldata_recall(uint32_t);
const properties_t *get_properties(uint32_t);
void clear_all_config_prop_data(void);
void cal_done(void);
bool cal_collect(uint16_t);
void request_to_redraw(uint16_t);
bool app_measurement_sweep(bool,uint16_t);
void app_measurement_update_frequencies(void);
bool need_interpolate(freq_t,freq_t,uint16_t);
uint32_t si5351_get_harmonic_lvl(uint32_t);
void ui_message_box_draw(const char *,const char *);
typedef int event_bus_t;
typedef struct { int topic; } event_bus_message_t;
#define EVENT_CONFIGURATION_CHANGED 1
#define EVENT_STORAGE_UPDATED 2
void event_bus_publish(event_bus_t *,int,void *);
void event_bus_subscribe(event_bus_t *,int,void (*)(const event_bus_message_t *,void *),void *);
'''
HARNESS = r'''
#define _GNU_SOURCE
#include "nanovna.h"
#include "sys/config_service.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
properties_t current_props;
config_t config={._harmonic_freq_threshold=300000000,._IF_freq=12000,._xtal_freq=26000000,._band_mode=3};
uint8_t sweep_mode=1;
bool calibration_in_progress;
float measured[2][401][2];
static unsigned capture[3], writes, bad_write, fail_capture;
static uint8_t saved[32768];
void chThdYield(void) {}
void chSemObjectInit(semaphore_t *s,int n) { *s=n; }
msg_t chSemWaitTimeout(semaphore_t *s,unsigned t) { (void)t; assert(*s==1); *s=0; return MSG_OK; }
void chSemSignal(semaphore_t *s) { assert(*s==0); *s=1; }
void event_bus_publish(event_bus_t *b,int t,void *d) { (void)b;(void)t;(void)d; }
void event_bus_subscribe(event_bus_t *b,int t,void (*f)(const event_bus_message_t *,void *),void *d) {
  (void)b;(void)t;(void)f;(void)d;
}
void flash_program_half_word_buffer(uint16_t *dst,uint16_t *data,uint16_t size) {
  uintptr_t a=(uintptr_t)dst;
  assert(a>=SAVE_PROP_CONFIG_ADDR && a+size<=0x08040000u);
  memset(dst,255,(size+2047u)&~2047u); memcpy(dst,data,size); writes++;
  if (bad_write) { ((uint8_t *)dst)[size-1]^=1; bad_write=0; }
}
void flash_erase_pages(uint32_t addr,unsigned size) { memset((void *)(uintptr_t)addr,255,size); }
void request_to_redraw(uint16_t m) { (void)m; }
void app_measurement_update_frequencies(void) {}
bool need_interpolate(freq_t a,freq_t b,uint16_t n) {
  return a!=cal_frequency0 || b!=cal_frequency1 || n!=cal_sweep_points;
}
static uint32_t harmonic_split=300000000;
uint32_t si5351_get_harmonic_lvl(uint32_t f) { return f>=harmonic_split; }
void ui_message_box_draw(const char *a,const char *b) { (void)a;(void)b; }
bool app_measurement_sweep(bool breaking,uint16_t mask) {
  (void)breaking; (void)mask; return true;
}
'''
