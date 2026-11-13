"""Run the production SD loader against EOF and injected FatFS read errors."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'src/ui/menus/menu_storage.c').read_text()


def function(name):
    start = text.index('static FILE_LOAD_CALLBACK(' + name + ') {')
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
static bool strcmpi(const char *a,const char *b) { return strcasecmp(a,b)==0; }
typedef unsigned UINT;
typedef uint32_t freq_t;
typedef int FIL, FILINFO, FRESULT;
#define FR_OK 0
#define SWEEP_POINTS_MAX 4
#define FREQUENCY_MAX 2700000000u
#define FREQUENCY_MIN 50000u
#define FMT_S2P_FILE 1
#define ST_START 0
#define ST_STOP 1
#define REDRAW_PLOT 1
#define FILE_LOAD_CALLBACK(name) const char* name(FIL* f, FILINFO* fno, uint8_t format)
static char spi_buffer[1024];
static float measured[2][SWEEP_POINTS_MAX][2];
static struct { float _electrical_delay[2]; unsigned _sweep_points; } current_props;
static unsigned reads, fail_at, generations, pauses, redraws;
static uint32_t limits[2];
static bool writing, valid;
static const char *contents;
static FRESULT f_read(FIL *f,void *buf,UINT n,UINT *size) {
  (void)f; assert(writing && !valid);
  if (++reads == fail_at) return 1;
  *size = reads == 1 ? strlen(contents) : 0;
  assert(*size <= n); memcpy(buf,contents,*size); return FR_OK;
}
static int parse_line(char *s,char **args,int max) {
  int n=0;
  for (char *p=strtok(s," \t");p && n<max;p=strtok(NULL," \t")) args[n++]=p;
  return n;
}
static uint32_t my_atoui(const char *s) { return strtoul(s,NULL,10); }
static float my_atof(const char *s) { return strtof(s,NULL); }
static void pause_sweep(void) { pauses++; }
static void set_sweep_frequency(unsigned mode,uint32_t f) { limits[mode]=f; }
static void request_to_redraw(unsigned r) { (void)r; redraws++; }
static void sweep_service_begin_measurement(void) { assert(!writing); writing=true; valid=false; }
static void sweep_service_end_measurement(void) { assert(writing); writing=false; }
static void sweep_service_increment_generation(void) { assert(!writing); generations++; valid=true; }
''' + function('load_snp_data') + '\n' + function('load_snp') + r'''
static const char *run(unsigned fail,uint8_t format) {
  reads=pauses=redraws=0; fail_at=fail;
  current_props._sweep_points=4; limits[0]=100; limits[1]=200;
  return load_snp(NULL,NULL,format);
}
int main(void) {
  contents="1000000 0.1 0.2\r\n2000000 0.3 0.4\r\n";
  assert(run(0,0)==NULL && valid && generations==1);
  assert(current_props._sweep_points==2 && pauses==1 && redraws==1);
  assert(limits[0]==1000000 && limits[1]==2000000);
  assert(measured[1][0][0]==0 && measured[0][1][0]==0.3f);
  // Failure after parsed rows must not publish or adopt a partial file's range.
  assert(strcmp(run(2,0),"Read error")==0);
  assert(!writing && !valid && generations==1);
  assert(current_props._sweep_points==4 && pauses==0 && redraws==0);
  assert(limits[0]==100 && limits[1]==200);
  assert(strcmp(run(1,0),"Read error")==0 && !valid && generations==1);
  contents="";
  assert(strcmp(run(0,0),"Empty file")==0 && !valid && generations==1);
  contents="1000000 0.1\r\n";
  assert(run(0,0)!=NULL && !valid);
  contents="1000000 0.1 0.2 0.3 0.4\r\n2000000 0.2 0.3 0.4 0.5\r\n";
  assert(run(0,FMT_S2P_FILE)==NULL && valid && generations==2);
  assert(measured[1][0][0]==0.3f && measured[1][0][1]==0.4f);
  puts("[PASS] SD imports: complete files, empty input and first/partial read failures");
}
'''
with tempfile.TemporaryDirectory(prefix='nanovna-import-') as d:
    path = Path(d)
    (path / 'test.c').write_text(source)
    subprocess.run(shlex.split(os.environ.get('HOST_CC', 'gcc')) + [
        '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'),
        '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
