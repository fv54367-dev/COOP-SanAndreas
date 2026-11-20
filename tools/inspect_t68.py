"""Read-only T68 evidence from the original SCM; no game execution."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import inspect_t64 as source

start, size = 0xB69A0, 41159
fingerprint = 2166136261
for byte in source.data[start:start + size]:
    fingerprint = ((fingerprint ^ byte) * 16777619) & 0xFFFFFFFF
if fingerprint != 0xDA1FBDD4:
    raise SystemExit(f"Unsupported GUNS1: FNV {fingerprint:08X}")
lines = dict(source.out)
checks = {
    0xB9A52: "ADD_VAL_TO_INT_LVAR 35@ 1",
    0xB9A65: "DELETE_OBJECT 117@",
    0xBD119: "IS_CHAR_HOLDING_OBJECT",
    0xBD13E: "SET_LVAR_INT 60@ 0",
    0xBD1B0: "IS_INT_LVAR_EQUAL_TO_NUMBER 60@ 1",
    0xBD1B7: "IS_INT_LVAR_EQUAL_TO_NUMBER 51@ 6",
    0xBD1DE: "LOCATE_CHAR_ON_FOOT_3D",
    0xBD264: "IS_BUTTON_PRESSED 0 15",
    0xBD5C9: "SET_LVAR_INT 60@ 0",
    0xBD607: "SET_LVAR_INT 52@ 0",
    0xBD60E: "SET_LVAR_INT 33@ 0",
    0xBD666: "WAIT 0",
    0xBD67A: "DELETE_OBJECT 117(36@,7)",
    0xBAA62: "SET_OBJECT_AREA_VISIBLE 117(36@,7) 0",
    0xBAD13: "SET_OBJECT_AREA_VISIBLE 117(36@,7) 8",
}
for offset, expected in checks.items():
    line = lines.get(offset, "")
    if expected not in line:
        raise SystemExit(f"Unexpected command {offset:08X}: {line}")
    print(line)
# Jump operands are mission-relative, negative offsets.
assert "GOTO_IF_FALSE -28121" in lines[0xBD1BE]
assert start + 28121 == 0xBD779
assert "GOTO -27785" in lines[0xBD66A]
assert start + 27785 == 0xBD629
print("PASS: original fingerprint, delivery gates, yielding cleanup and area commands")
print("Evidence only: does not implement or test cooperative delivery.")
