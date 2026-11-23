"""Firmware, calibration slots, and remembered state must occupy separate flash regions."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
header=(root/'include/driver/stm32_peripherals.h').read_text()
layouts=header.split('// For STM32F303xC CPU setting',1)[1].split('// Save config_t and properties_t flash area',1)[0]
large,small=layouts.split('#else',1)
budgets={'STM32F303': 114*1024, 'STM32F072': 98*1024}
for board in ('STM32F303','STM32F072'):
    path=next((root/'boards'/board).glob('*.ld'))
    text=path.read_text()
    regions={n:(int(a,16),int(s)*1024) for n,a,s in re.findall(r'(flash[07])\s*:\s*org\s*=\s*(0x[0-9a-fA-F]+),\s*len\s*=\s*(\d+)k',text)}
    assert regions['flash0'][0]+regions['flash0'][1]<=regions['flash7'][0],regions
    assert regions['flash0'][1]==budgets[board],regions
    values={n:int(v,0) for n,v in re.findall(r'^#define\s+(\w+)\s+(0[xX][\da-fA-F]+|\d+)U?\s*$',
                                              large if board=='STM32F303' else small, re.M)}
    page=values['FLASH_PAGESIZE']
    sector,sector_size=regions['flash7']
    record_end=values['FLASH_CALIBRATION_SECTOR_START']+values['FLASH_CALIBRATION_SECTOR_SIZE']
    slot0=record_end-values['SAVE_CONFIG_SIZE']-values['SAVEAREA_MAX']*values['SAVE_PROP_CONFIG_SIZE']
    state=sector+sector_size-page if board=='STM32F303' else sector
    assert state%page==0 and sector<=state and state+page<=sector+sector_size
    assert state+page<=slot0 or state>=record_end
    if board=='STM32F303':
        # CAL staging starts just after the saved records and fits in one slot's capacity.
        assert record_end+values['SAVE_PROP_CONFIG_SIZE']<=state
    print(board, 'code boundary:', hex(sector), 'remembered state page:', hex(state))
