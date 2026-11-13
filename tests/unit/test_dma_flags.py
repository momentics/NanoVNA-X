"""DMA callbacks must not acknowledge another channel's pending interrupt."""
from pathlib import Path
import os,re,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
text=(root/'boards/STM32F303/dma_v1.c').read_text()
source="""
#include <stdint.h>
#include <assert.h>
static struct {uint32_t ISR,IFCR;} regs;
#define DMA1 (&regs)
#define STM32_DMA_ISR_MASK 15u
#define OSAL_IRQ_HANDLER(name) void name(void)
#define OSAL_IRQ_PROLOGUE()
#define OSAL_IRQ_EPILOGUE()
static unsigned calls,seen;
static void callback(unsigned flags) {++calls;seen=flags;}
"""
for i in range(1,8):source+=f'#define DMA1_CH{i}_HANDLER_FUNC callback\n'
names=[]
for match in re.finditer(r'OSAL_IRQ_HANDLER\((\w+)\)\s*\{',text):
    pos=match.end();depth=1
    while depth:depth+=(text[pos]=='{')-(text[pos]=='}');pos+=1
    names.append(match[1]);source+=text[match.start():pos]+'\n'
source+='int main(void) {\n'
for i,name in enumerate(names):
    mask=f'(15u<<{4*i})'
    source+=f'regs.ISR=0x0fffffff;calls=0;{name}();assert(regs.IFCR=={mask} && calls==1 && seen==15);\n'
    source+=f'regs.ISR=0x0fffffff & ~{mask};calls=0;{name}();assert(!regs.IFCR && !calls);\n'
source+='}\n'
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.c').write_text(source)
    subprocess.run(shlex.split(os.environ.get('HOST_CC','gcc'))+['-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: all seven H4 DMA handlers preserve other channels and ignore unrelated flags')
