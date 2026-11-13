"""Model a nested timer wakeup inside the production DMA/EXTI handlers.

The timer cannot switch threads until the outermost ISR exits. Every compact
driver wrapper must run the RTOS exit hook, even if its own flags are empty.
"""
from pathlib import Path
import os, re, shlex, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
for board in ('STM32F303','STM32F072'):
    for driver in ('dma_v1.c','exti_v1.c'):
        text=(root/'boards'/board/driver).read_text()
        functions=[]
        for match in re.finditer(r'OSAL_IRQ_HANDLER\((\w+)\)\s*\{',text):
            pos=match.end(); depth=1
            while depth:
                depth+=(text[pos]=='{')-(text[pos]=='}'); pos+=1
            functions.append((match[1],text[match.start():pos]))
        source=r'''
#include <stdint.h>
#include <assert.h>
static unsigned depth, entries, exits, callbacks, switches, ready;
static struct { uint32_t ISR,IFCR,PR; } regs;
#define DMA1 (&regs)
#define EXTI (&regs)
#define STM32_DMA_ISR_MASK 15u
#define OSAL_IRQ_HANDLER(name) void name(void)
static void enter(void) { ++depth; ++entries; }
static void leave(void) {
  assert(depth); --depth; ++exits;
  if(!depth && ready) { ready=0; ++switches; }
}
#define OSAL_IRQ_PROLOGUE() enter()
#define OSAL_IRQ_EPILOGUE() leave()
static void callback(unsigned flags) {
  (void)flags; assert(depth==1); ++callbacks;
  enter(); ready=1; leave(); // Higher-priority timer wakes the sleeping sweep.
  assert(switches==0); // Nested ISR must defer switching to the outer ISR.
}
'''
        for i in range(1,8): source+=f'#define DMA1_CH{i}_HANDLER_FUNC callback\n'
        for i in range(16): source+=f'#define EXT_CH{i}_HANDLER_FUNC callback\n'
        source+='\n'.join(body for _,body in functions)
        source+='\nint main(void) {\n'
        for name,_ in functions:
            source+=f'''
  depth=entries=exits=callbacks=switches=ready=0;
  regs.ISR=0xffffffff; regs.PR=0xffff;
  {name}();
  assert(depth==0 && callbacks>0 && switches==1 && entries==exits);
  depth=entries=exits=callbacks=switches=0; ready=1;
  regs.ISR=0; regs.PR=0;
  {name}();
  assert(depth==0 && callbacks==0 && switches==1 && entries==1 && exits==1);
'''
        source+='}\n'
        with tempfile.TemporaryDirectory() as d:
            p=Path(d); (p/'irq.c').write_text(source)
            subprocess.run(shlex.split(os.environ.get('HOST_CC','gcc'))+
                           ['-std=c11','-Wall','-Wextra','-Werror',str(p/'irq.c'),'-o',str(p/'irq')],check=True)
            subprocess.run([str(p/'irq')],check=True)
        print(f'{board} {driver}: {len(functions)} IRQ wrappers passed nested-wakeup/empty-IRQ tests')
