"""Production CAL import is transactional; binary SD transfers require exact sizes."""
from pathlib import Path
import ast
import os
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
def literal(name):
    for node in ast.parse((ROOT/'tests/unit/calibration_fixture.py').read_text()).body:
        if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in node.targets):
            return ast.literal_eval(node.value)
    raise ValueError(name)
def function(signature):
    text=(ROOT/'src/ui/menus/menu_storage.c').read_text()
    a=text.index(signature);b=text.index('{',a)+1;depth=1
    while depth:
        depth+=(text[b]=='{')-(text[b]=='}');b+=1
    return text[a:b]

source=literal('HARNESS').split('int main(void) {')[0]+r'''
typedef unsigned UINT;
typedef int FRESULT;
typedef struct {const void *data;unsigned length,position,calls,fail_at,short_at;} FIL;
typedef struct {unsigned fsize;} FILINFO;
#define FR_OK 0
#define FR_DISK_ERR 1
#define FILE_LOAD_CALLBACK(name) const char *name(FIL *f,FILINFO *fno,uint8_t format)
#define FILE_SAVE_CALLBACK(name) FRESULT name(FIL *f,uint8_t format)
static uint32_t spi_buffer[512];
static unsigned adopted;
static FRESULT f_read(FIL *f,void *data,UINT count,UINT *size) {
  ++f->calls;
  if(f->calls==f->fail_at) return FR_DISK_ERR;
  if(count>f->length-f->position) count=f->length-f->position;
  if(f->calls==f->short_at && count) --count;
  *size=count;memcpy(data,(const char *)f->data+f->position,count);f->position+=count;
  return FR_OK;
}
static FRESULT f_write(FIL *f,const void *data,UINT count,UINT *size) {
  (void)data; ++f->calls; *size=f->short_at==f->calls ? count-1 : count;
  return f->fail_at==f->calls ? FR_DISK_ERR : FR_OK;
}
static int load_properties(unsigned slot) {assert(slot==NO_SAVE_SLOT);++adopted;return 0;}
static void load_default_properties(void) { memset(&current_props,0,sizeof current_props); }
#ifdef TEST_SMALL_H
#undef NANOVNA_F303
#endif
'''
source+='\n'.join(function(signature) for signature in (
    'static FRESULT read_exact(', 'static FRESULT write_exact(',
    'static FILE_LOAD_CALLBACK(load_cal)', 'static FILE_SAVE_CALLBACK(save_cal)'))
source+=r'''
#ifndef TEST_SMALL_H
int main(void) {
#ifdef _WIN32
  assert(VirtualAlloc((void *)0x08000000,0x40000,MEM_RESERVE|MEM_COMMIT,
      PAGE_READWRITE)==(void *)0x08000000);
#else
  assert(mmap((void *)0x08000000,0x40000,PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)0x08000000);
#endif
  memset((void *)0x08000000,255,0x40000); config_service_init();
  assert(properties_stage_write(0xfffff800u,spi_buffer,2048)==-1);
  properties_t incoming={.magic=PROPERTIES_MAGIC,._frequency0=14000000,._frequency1=14350000,
    ._cal_frequency0=14000000,._cal_frequency1=14350000,._sweep_points=101,._cal_sweep_points=101,
    ._portz=50,._cal_load_r=50};
  for(unsigned i=0;i<TRACES_MAX;++i) incoming._trace[i].scale=1;
  properties_update_checksum(&incoming); assert(properties_valid(&incoming));
  current_props=incoming; current_props.magic=0x12345678u;
  properties_t previous=current_props;
  FILINFO info={sizeof incoming};
  for(unsigned failure=1;failure<=9;++failure) {
    FIL f={.data=&incoming,.length=sizeof incoming,.fail_at=failure};
    if(failure<=(sizeof incoming+2047)/2048) {
      assert(load_cal(&f,&info,0)!=NULL && !adopted);
      assert(!memcmp(&current_props,&previous,sizeof previous));
    }
  }
  FIL short_file={.data=&incoming,.length=sizeof incoming,.short_at=2};
  assert(load_cal(&short_file,&info,0)!=NULL && !adopted);
  assert(!memcmp(&current_props,&previous,sizeof previous));
  FIL good={.data=&incoming,.length=sizeof incoming};
  assert(load_cal(&good,&info,0)==NULL && adopted==1);
  assert(current_props.magic==PROPERTIES_MAGIC && !memcmp(&incoming,&current_props,sizeof incoming));
  assert(*(uint32_t *)SAVE_PROP_CONFIG_ADDR==0xffffffffu); // No saved slot was written.
  incoming._sweep_points=402; properties_update_checksum(&incoming);
  good=(FIL){.data=&incoming,.length=sizeof incoming};
  previous=current_props;
  assert(load_cal(&good,&info,0)!=NULL && adopted==1);
  assert(!memcmp(&current_props,&previous,sizeof previous));
  incoming._sweep_points=101;incoming._trace[0].type=255;properties_update_checksum(&incoming);
  assert(!properties_valid(&incoming));
  incoming._trace[0].type=0;incoming._cal_data[0][0][0]=NAN;properties_update_checksum(&incoming);
  assert(!properties_valid(&incoming));
  incoming._cal_data[0][0][0]=0;incoming._trace[0].refpos=INFINITY;properties_update_checksum(&incoming);
  assert(!properties_valid(&incoming));
  FIL full={.short_at=1};assert(save_cal(&full,0)==FR_DISK_ERR);
  FIL failed={.fail_at=1};assert(save_cal(&failed,0)==FR_DISK_ERR);
  FIL complete={0};assert(save_cal(&complete,0)==FR_OK);
  puts("SD CAL: valid atomic import, stale magic, truncated/failed reads, bounds, short/full-card writes passed");
}
#else
int main(void) {
  properties_t incoming={.magic=PROPERTIES_MAGIC,._frequency0=14000000,._frequency1=14350000,
    ._cal_frequency0=14000000,._cal_frequency1=14350000,._sweep_points=101,._cal_sweep_points=101,
    ._portz=50,._cal_load_r=50};
  for(unsigned i=0;i<TRACES_MAX;++i) incoming._trace[i].scale=1;
  properties_update_checksum(&incoming);
  FILINFO info={sizeof incoming};
  current_props=incoming;
  FIL f={.data=&incoming,.length=sizeof incoming,.short_at=1};
  assert(load_cal(&f,&info,0)!=NULL && !adopted);
  assert(!memcmp(&current_props,&incoming,sizeof incoming));
  f=(FIL){.data=&incoming,.length=sizeof incoming,.short_at=2};
  assert(load_cal(&f,&info,0)!=NULL && adopted==1);
  assert(!current_props.magic && !current_props._cal_status);
  f=(FIL){.data=&incoming,.length=sizeof incoming};
  assert(load_cal(&f,&info,0)==NULL && adopted==2);
  assert(!memcmp(&current_props,&incoming,sizeof incoming));
  incoming._trace[0].type=255;properties_update_checksum(&incoming);
  f=(FIL){.data=&incoming,.length=sizeof incoming};
  assert(load_cal(&f,&info,0)!=NULL && !current_props._cal_status);
  puts("H SD CAL: invalid/short imports fail closed; valid import succeeds");
}
#endif
'''
with tempfile.TemporaryDirectory(prefix='nanovna-storage-') as d:
    tmp=Path(d)
    (tmp/'nanovna.h').write_text(literal('HEADER'))
    for rel in ('ch.h','hal.h','rf/sweep.h','ui/core/ui_core.h','driver/si5351.h','sys/event_bus.h'):
        p=tmp/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "nanovna.h"\n')
    (tmp/'test.c').write_text(source)
    subprocess.run(shlex.split(os.environ.get('HOST_CC','gcc'))+[
        '-std=c11','-O2','-I'+str(tmp),'-I'+str(ROOT/'include'),str(tmp/'test.c'),
        *[str(ROOT/p) for p in ('src/processing/calibration.c','src/sys/config_service.c')],
        '-lm','-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
    subprocess.run(shlex.split(os.environ.get('HOST_CC','gcc'))+[
        '-std=c11','-O2','-DTEST_SMALL_H','-I'+str(tmp),'-I'+str(ROOT/'include'),str(tmp/'test.c'),
        *[str(ROOT/p) for p in ('src/processing/calibration.c','src/sys/config_service.c')],
        '-lm','-o',str(tmp/'test-h')],check=True)
    subprocess.run([str(tmp/'test-h')],check=True)
