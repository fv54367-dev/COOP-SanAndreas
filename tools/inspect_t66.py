"""Inspect the original mission ranges for T66; never writes the game SCM."""
from pathlib import Path
import sys, json, struct, re
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / '_refs/sa-coop-karimov/tools/re'))
import scm_disasm as scm
DATA = (ROOT.parent / 'GTA_JUGADOR1/data/script/main.scm').read_bytes()
DB = json.loads((ROOT / 'sdk/Sanny Builder 4/data/sa_sbl_coopandreas/sa_coop.json').read_text(encoding='utf-8'))
CMDS = {int(c['id'], 16): c for e in DB['extensions'] for c in e['commands']}
def inspect():
    source = (ROOT/'client/src/CMissionGuestActions.cpp').read_text(encoding='utf-8')
    specs = re.findall(r'\{"(HEIST2|WUZI2|RYDER3)", (0x[0-9A-F]+), (\d+), (0x[0-9A-F]+)', source)
    assert len(specs) == 3
    for name, start, length, expected in specs:
        start, length = int(start,16), int(length)
        h = 2166136261
        for v in DATA[start:start+length]: h = ((h ^ v) * 16777619) & 0xffffffff
        assert h == int(expected,16), name
        print(f'PASS {name}: start={start:06X} length={length} FNV={h:08X}')
    for a, b in [(0x23B69E,0x23BA45),(0x16ED89,0x16EE3F)]:
        while a < b:
            line,a = scm.disasm_one(DATA,a,CMDS)
            print(line)
    # Catalyst: exact native creation/physics/catch logic, independent of modified text offsets.
    a, end = 0xC0A67, 0xC0A67+20209
    lines=[]
    while a < end:
        old=a; line,a=scm.disasm_one(DATA,a,CMDS); lines.append((old,line))
    hits=set()
    for i,(_,line) in enumerate(lines):
        if any(k in line for k in ['CREATE_OBJECT_NO_OFFSET 127@','SET_OBJECT_VELOCITY 110','IS_INT_LVAR_EQUAL_TO_NUMBER 89@ 10']):
            hits.update(range(max(0,i-6),min(len(lines),i+12)))
    for i,(_,line) in enumerate(lines):
        if i in hits: print(line)
if __name__ == '__main__': inspect()
