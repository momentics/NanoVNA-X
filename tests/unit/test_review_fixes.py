"""Regression cases contributed by @jebcampbellhm in PR #13, adapted to NanoVNA-X."""
from pathlib import Path
import ast
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CC = shlex.split(os.environ.get('HOST_CC', 'gcc'))


def body(path, signature):
    text = (ROOT / path).read_text()
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def literal(path, name):
    for node in ast.parse((ROOT / path).read_text()).body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise ValueError(name)


def run(name, source, extra=(), headers=None):
    with tempfile.TemporaryDirectory(prefix='nanovna-review-') as d:
        tmp = Path(d)
        for path, content in (headers or {}).items():
            dest = tmp / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(content)
        (tmp / 'probe.c').write_text(source)
        flags = ['-O2']
        subprocess.run(CC + ['-std=c11', *flags, '-I'+str(tmp), '-I'+str(ROOT/'include'),
                            str(tmp/'probe.c'), *[str(ROOT/p) for p in extra], '-lm', '-o', str(tmp/'probe')], check=True)
        result = subprocess.run([str(tmp/'probe')], text=True, capture_output=True)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        print(f'PASS {name}: {result.stdout.strip()}')



prefix = '#include <stdint.h>\n#include <stdbool.h>\n#include <assert.h>\n#include <stdio.h>\n#include <math.h>\n'

helpers = '\n'.join(body('include/ui/draw/plot_internal.h', 'static inline float '+n+'(')
                    for n in ('get_l', 'get_s21_r', 'get_s21_x'))
callbacks = '\n'.join(body('src/ui/draw/traces.c', 'float '+n+'(')
                      for n in ('s21series_r', 's21shunt_r', 's21series_x', 's21shunt_x'))
run('S21 impedance', prefix+'#define PORT_Z 50.0f\n'+helpers+'\n'+callbacks+r'''
int main(void) {
  float half[2]={0.5f,0}, reactive[2]={0.8f,-0.4f};
  float series=s21series_r(0,half), shunt=s21shunt_r(0,half), x=s21series_x(0,reactive);
  assert(fabsf(series-100)<0.01f && fabsf(shunt-25)<0.01f && fabsf(x-50)<0.01f);
  // Independently construct S21 for arbitrary series and shunt impedances.
  const float loads[][2]={{0,50},{50,-25},{100,200},{25,0}};
  for(unsigned i=0;i<4;++i) {
    float r=loads[i][0],j=loads[i][1],d=(100+r)*(100+r)+j*j;
    float v[]={100*(100+r)/d,-100*j/d};
    assert(fabsf(s21series_r(0,v)-r)<0.001f && fabsf(s21series_x(0,v)-j)<0.001f);
    d=(25+r)*(25+r)+j*j;
    v[0]=(r*(25+r)+j*j)/d; v[1]=25*j/d;
    assert(fabsf(s21shunt_r(0,v)-r)<0.001f && fabsf(s21shunt_x(0,v)-j)<0.001f);
  }
  printf("100-ohm series reads %.0f; 25-ohm shunt reads %.0f; +j50 series reads +j%.0f\n",series,shunt,x);
}
''')

run('L/C point frequency', prefix+r'''
typedef uint32_t freq_t;
enum { ST_START,ST_STOP,ST_CENTER,ST_CW,ST_SPAN,ST_STEP,ST_VAR };
static freq_t frequency0=14000000, frequency1=14350000;
static freq_t get_frequency(unsigned i) { return frequency0+(frequency1-frequency0)*i/100; }
#define VNA_PI 3.14159265358979323846f
'''+body('include/nanovna.h','static inline freq_t\nget_sweep_frequency(')+'\n'+
body('include/ui/draw/plot_internal.h','static inline float get_w(')+r'''
int main(void) {
  assert(fabsf(get_w(50)/(2*VNA_PI)-14175000)<2);
  for (unsigned i=0;i<=100;++i) assert(fabsf(get_w(i)/(2*VNA_PI)-get_frequency(i))<2);
  frequency1=frequency0; assert(fabsf(get_w(50)/(2*VNA_PI)-frequency0)<2);
  puts("point frequencies and CW use the actual measurement frequency");
}
''')

header = literal('tests/unit/calibration_fixture.py', 'HEADER')
harness = literal('tests/unit/calibration_fixture.py', 'HARNESS').split('int main(void) {')[0]
old = harness[harness.index('bool app_measurement_sweep('):len(harness)]
harness = harness.replace(old, 'static bool probe_fail=true; bool app_measurement_sweep(bool b,uint16_t m) { (void)b;(void)m; return !probe_fail; }\n')
headers = {'nanovna.h': header}
for name in ('ch.h','hal.h','rf/sweep.h','ui/core/ui_core.h','driver/si5351.h','sys/event_bus.h'):
    headers[name] = '#include "nanovna.h"\n'
run('ordinary CAL failure and bandwidth', harness+r'''
int main(void) {
  current_props.magic=PROPERTIES_MAGIC;
  frequency0=cal_frequency0=14000000; frequency1=cal_frequency1=14350000;
  sweep_points=cal_sweep_points=101; config._bandwidth=500;
  measured[0][0][0]=0.123f;
  assert(!cal_collect(CAL_OPEN));
  assert(!(cal_status & CALSTAT_OPEN));
  assert(cal_data[CAL_OPEN][0][0]==0 && config._bandwidth==500);
  probe_fail=false;
  measured[0][50][1]=NAN;
  assert(!cal_collect(CAL_OPEN) && !(cal_status & CALSTAT_OPEN));
  measured[0][50][1]=0;
  assert(cal_collect(CAL_OPEN) && (cal_status & CALSTAT_OPEN));
  assert(cal_data[CAL_OPEN][0][0]==0.123f && config._bandwidth==500);
  puts("failed/nonfinite captures leave CAL unchanged; retry succeeds; 16-bit bandwidth preserved");
}
''', ('src/processing/calibration.c','src/sys/config_service.c'), headers)

# Entry points reject one point; the shared generator is defensive as well.
text = (ROOT/'src/rf/sweep.c').read_text()
start = text.rindex('void app_measurement_set_frequencies(')
freq_code = text[start:text.index('freq_t get_frequency(', start)]
run('single-point frequency generator', prefix+r'''
typedef uint32_t freq_t;
static freq_t _f_start,_f_points,_f_delta,_f_error;
static bool sweep_data_valid;
'''+freq_code+r'''
int main(int argc,char **argv) { (void)argv; app_measurement_set_frequencies(14000000,14350000,argc); return _f_delta; }
''')

run('disabled marker lookup', prefix+r'''
typedef uint32_t freq_t;
#define MARKERS_MAX 8
static struct { uint8_t enabled,reserved; uint16_t index; freq_t frequency; } markers[8];
'''+body('src/runtime/runtime_entry.c','freq_t get_marker_frequency(')+r'''
int main(int argc,char **argv) { (void)argv; assert(get_marker_frequency(-argc)==0); assert(get_marker_frequency(8)==0); markers[0].enabled=1; markers[0].frequency=14000000; assert(get_marker_frequency(0)==14000000); return 0; }
''')

run('unterminated empty quoted argument', prefix+
body('src/core/common.c','static inline char* _strpbrk(')+'\n'+
body('src/core/common.c','int parse_line(')+r'''
int main(void) { char line[]="\""; char *args[8]; assert(parse_line(line,args,8)==-1); return 0; }
''')

run('left search without a crossing', prefix+r'''
#define VNA_EPSILON 1e-12f
#define vna_sqrtf sqrtf
#define MARKER_INVALID -1
typedef float (*get_value_t)(uint16_t);
static unsigned sweep_points=5;
static uint32_t get_frequency(uint16_t i) { return 1000000+i*1000000; }
static uint32_t get_frequency_step(void) { return 1000000; }
static void set_marker_index(int a,int b) { (void)a;(void)b; }
static float flat(uint16_t i) { (void)i; return 1; }
'''+body('src/rf/analysis.c','void match_quadratic_equation(')+'\n'+
body('src/rf/analysis.c','float measure_search_value(')+r'''
int main(void) { uint16_t i=3; float f=measure_search_value(&i,0,flat,-1,-1);
assert(f==0); puts("no fabricated crossing at left edge"); }
''')

run('CW rectangular grid', prefix+r'''
typedef uint32_t freq_t;
#define WIDTH 400
#define CELLOFFSETX 0
#define GRID_BITS 7
static uint16_t grid_offset,grid_width;
'''+body('src/ui/draw/grid.c','void update_grid(')+'\n'+
body('src/ui/draw/grid.c','int rectangular_grid_x(')+r'''
int main(int argc,char **argv) {
  (void)argv; update_grid(14000000,14000000); return rectangular_grid_x(argc);
}
''')

run('Smith chart coordinate', prefix+r'''
#define WIDTH 400
#define HEIGHT 240
#define GRIDY 30
#define CELLOFFSETX 0
#define P_CENTER_X 200
#define P_CENTER_Y 120
#define P_RADIUS 120
enum {TRC_SMITH,TRC_SWR};
#define ROUND_GRID_MASK (1u << TRC_SMITH)
#define RECTANGULAR_GRID_MASK (1u << TRC_SWR)
typedef float (*get_value_cb_t)(int,const float*);
static struct { int type,channel; } trace[1]={{TRC_SMITH,0}};
static struct { get_value_cb_t get_value_cb; } trace_info_list[2];
static float measured[1][1][2]={{{1.0f/3.0f,0}}};
static uint16_t sweep_points=1, plotted_x,plotted_y;
typedef int trace_index_table_t;
typedef struct { int unused; } MarkLineState;
static int trace_index_table(int t) {return t;}
static float get_trace_refpos(int t) {(void)t;return 0;}
static float get_trace_scale(int t) {(void)t;return 1;}
static int float2int(float v) {return (int)roundf(v);}
static float infinityf(void) {return INFINITY;}
static void mark_set_index(int a,int b,uint16_t x,uint16_t y,MarkLineState *s) {
  (void)a;(void)b;(void)s;plotted_x=x;plotted_y=y;
}
'''+body('src/ui/draw/traces.c','static inline void cartesian_scale(')+'\n'+
body('src/ui/draw/traces.c','void trace_into_index(')+r'''
int main(void) {
  trace_into_index(0); assert(plotted_x==240 && plotted_y==120);
  measured[0][0][0]=0;trace_into_index(0);assert(plotted_x==200 && plotted_y==120);
  measured[0][0][0]=1;trace_into_index(0);assert(plotted_x==320 && plotted_y==120);
  measured[0][0][0]=-1;trace_into_index(0);assert(plotted_x==80 && plotted_y==120);
  measured[0][0][0]=0;measured[0][0][1]=0.5f;
  trace_into_index(0);assert(plotted_x==200 && plotted_y==60);
  puts("100-ohm load at Z0=50 plots correctly at 40 pixels from center");
}
''')

run('non-50-ohm calibration renormalization', prefix+r'''
#define __VNA_Z_RENORMALIZATION__
#define SWEEP_CH0_MEASURE 1
typedef struct { int unused; } measurement_pipeline_t;
static struct {float _portz,_cal_load_r;} current_props={50,75};
#define cal_load_r current_props._cal_load_r
static uint16_t sweep_points=1;
static float measured[1][1][2];
static bool app_measurement_sweep(bool b,uint16_t m) {(void)b;(void)m;return true;}
'''+body('src/rf/pipeline.c','void measurement_pipeline_renormalize(')+'\n'+body('src/rf/pipeline.c','bool measurement_pipeline_execute(')+r'''
int main(void) {
  assert(measurement_pipeline_execute(NULL,false,1));
  assert(fabsf(measured[0][0][0]-0.2f)<1e-6f);
  current_props._portz=75; measured[0][0][0]=0;
  assert(measurement_pipeline_execute(NULL,false,1) && measured[0][0][0]==0);
  puts("75-ohm reference converts correctly to 50 ohms and remains unchanged at 75 ohms");
}
''')

scan_text=(ROOT/'src/sys/shell_commands.c').read_text()
scan_defines=scan_text[scan_text.index('#define SCAN_MASK_OUT_FREQ'):scan_text.index('VNA_SHELL_FUNCTION(cmd_scan)')]
run('USB scan rejection and failed capture', prefix+r'''
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#define NANOVNA_HOST_TEST
#define define_to_STR(x) "401"
#define VNA_SHELL_FUNCTION(name) void name(int argc,char **argv)
#define PRINT_USAGE(...) shell_printf(__VA_ARGS__)
#define VNA_SHELL_NEWLINE_STR "\r\n"
#define VNA_FREQ_FMT_STR "%u"
#define FREQUENCY_MAX 2700000000u
#define SWEEP_POINTS_MAX 401
#define ST_START 0
#define ST_STOP 1
#define SWEEP_CH0_MEASURE 1
#define SWEEP_CH1_MEASURE 2
#define SWEEP_APPLY_CALIBRATION 4
#define SWEEP_APPLY_EDELAY_S11 8
#define SWEEP_APPLY_EDELAY_S21 16
#define SWEEP_APPLY_S21_OFFSET 32
#define SWEEP_USE_INTERPOLATION 64
#define CALSTAT_APPLY 256
#define chsnprintf snprintf
typedef uint32_t freq_t;
static uint16_t sweep_points=101,cal_status=CALSTAT_APPLY;
static float electrical_delayS11,electrical_delayS21,s21_offset,measured[2][401][2];
static unsigned captures,bytes,generations,renormalized,restored;
static bool complete;
static char message[128];
static freq_t get_sweep_frequency(int m) {return m==ST_START ? 14000000 : 14350000;}
static freq_t get_frequency(int i) {return 1000000+i*1000000;}
static uint32_t my_atoui(const char *p) {return strtoul(p,NULL,10);}
static int shell_printf(const char *fmt,...) {
  va_list ap;va_start(ap,fmt);int n=vsnprintf(message,sizeof message,fmt,ap);va_end(ap);return n;
}
static bool need_interpolate(freq_t a,freq_t b,unsigned n) {(void)a;(void)b;(void)n;return false;}
static void app_measurement_set_frequencies(freq_t a,freq_t b,unsigned n) {(void)a;(void)b;assert(n>=2 && n<=401);}
static void app_measurement_reset(void) {}
static bool app_measurement_sweep(bool b,unsigned m) {(void)b;(void)m;captures++;measured[0][0][0]=0.123f;return complete;}
static void measurement_pipeline_renormalize(unsigned m) {(void)m;renormalized++;}
static void sweep_service_increment_generation(void) {generations++;}
static void app_measurement_update_frequencies(void) {restored++;}
static void shell_stream_write(const void *p,unsigned n) {(void)p;bytes+=n;}
static void pause_sweep(void) {}
static void resume_sweep(void) {}
static void chThdYield(void) {}
'''+scan_defines+body('src/sys/shell_commands.c','VNA_SHELL_FUNCTION(cmd_scan)')+r'''
int main(void) {
  char *args[]={"1000000","2000000","1","131"};
  const char *invalid[]={"0","1","402","65537"};
  for(unsigned i=0;i<4;i++) {
    args[2]=(char *)invalid[i];cmd_scan(4,args);
    assert(captures==0 && bytes==0 && sweep_points==101);
  }
  args[2]="2";cmd_scan(4,args);
  assert(captures==1 && bytes==0 && !generations && !renormalized && restored==1);
  assert(strstr(message,"Capture failed") && sweep_points==101);
  complete=true;cmd_scan(4,args);
  assert(captures==2 && bytes==28 && generations==1 && renormalized==1 && sweep_points==101);
  puts("invalid counts rejected; failed scan sends no sample frame; successful binary scan preserved");
}
''')

run('RTC field validation', prefix+r'''
#include <stdlib.h>
#include <string.h>
#define __USE_RTC__
#define VNA_SHELL_FUNCTION(name) void name(int argc,char **argv)
#define VNA_SHELL_NEWLINE_STR "\r\n"
#define ARRAY_COUNT(x) (sizeof(x)/sizeof((x)[0]))
static unsigned writes;
static uint32_t rtc_get_tr_bcd(void) {return 0;}
static uint32_t rtc_get_dr_bcd(void) {return 0x240101;}
static uint32_t my_atoui(const char *s) {return strtoul(s,NULL,0);}
static float my_atof(const char *s) {return strtof(s,NULL);}
static int get_str_index(const char *s,const char *list) {
  unsigned i=0;while (*list) {const char *p=list;while (*list && *list!='|')++list;
    if (strlen(s)==(unsigned)(list-p) && !strncmp(p,s,list-p))return i;
    if (*list)++list;++i;}return -1;
}
static void rtc_set_time(uint32_t d,uint32_t t) {(void)d;(void)t;++writes;}
static void rtc_set_cal(float c) {(void)c;}
static int shell_printf(const char *fmt,...) {(void)fmt;return 1;}
'''+body('src/sys/shell_commands.c','VNA_SHELL_FUNCTION(cmd_time)')+r'''
int main(void) {
  char *month[]={"m","13"};cmd_time(2,month);assert(!writes);
  month[1]="12";cmd_time(2,month);assert(writes==1);
  char *seconds[]={"sec","60"};cmd_time(2,seconds);assert(writes==1);
  char *bcd[]={"b","0x241301","0x120000"};cmd_time(3,bcd);assert(writes==1);
  bcd[1]="0x241201";cmd_time(3,bcd);assert(writes==2);
}
''')
print('Audit regression cases passed.')
