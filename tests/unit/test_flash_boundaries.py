"""Linker code regions must stop before saved calibration; H4 staging fits above it."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
for board in ('STM32F303','STM32F072'):
    path=next((root/'boards'/board).glob('*.ld'))
    text=path.read_text()
    regions={n:(int(a,16),int(s)*1024) for n,a,s in re.findall(r'(flash[07])\s*:\s*org\s*=\s*(0x[0-9a-fA-F]+),\s*len\s*=\s*(\d+)k',text)}
    assert regions['flash0'][0]+regions['flash0'][1]<=regions['flash7'][0],regions
    print(board, 'code boundary:', hex(regions['flash7'][0]))
