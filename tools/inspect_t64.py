from pathlib import Path
import sys
import json

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root.parent / '_refs/sa-coop-karimov/tools/re'))
import scm_disasm as s
data = (root.parent / 'GTA_JUGADOR1/data/script/main.scm').read_bytes()
db = json.loads((root / 'sdk/Sanny Builder 4/data/sa_sbl_coopandreas/sa_coop.json').read_text(encoding='utf-8'))
cmds = {int(c['id'], 16): c for e in db['extensions'] for c in e['commands']}
pos = 0xb69a0
out, hits = [], set()
while pos < 0xb69a0 + 41159:
    old = pos
    line, pos = s.disasm_one(data, pos, cmds)
    out.append((old, line))
    if any(k in line for k in ['HOLDING_OBJECT', '154@ 155@', 'DROP_OBJECT', '$26820']):
        hits.update(range(max(0, len(out)-6), len(out)+20))
if __name__ == '__main__':
    print('\n'.join(line for i, (p, line) in enumerate(out) if i in hits))
