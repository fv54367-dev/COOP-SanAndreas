"""Read-only evidence for T69's native noise insertion and remaining crate work."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import inspect_t64 as source

start, size = 0xB69A0, 41159
fingerprint = 2166136261
for byte in source.data[start:start + size]:
    fingerprint = ((fingerprint ^ byte) * 16777619) & 0xFFFFFFFF
assert fingerprint == 0xDA1FBDD4, f"Unsupported original: {fingerprint:08X}"
lines = dict(source.out)
checks = {
    0xBAD5A: "CSET_VAR_INT_TO_LVAR_FLOAT $26820 167@",
    0xBAD66: "IS_INT_VAR_GREATER_OR_EQUAL_TO_NUMBER $26820 100",
    0xBA9D3: "IS_INT_LVAR_EQUAL_TO_NUMBER 94@ 1",
    0xBAA88: "SET_LVAR_INT 94@ 0",
    0xBAAFF: "IS_INT_LVAR_EQUAL_TO_NUMBER 37@ 8",
    0xBAB11: "LOCATE_CHAR_ANY_MEANS_3D",
    0xBAA62: "SET_OBJECT_AREA_VISIBLE 117(36@,7) 0",
    0xBAD13: "SET_OBJECT_AREA_VISIBLE 117(36@,7) 8",
    0xBD119: "IS_CHAR_HOLDING_OBJECT",
    0xBD1B0: "IS_INT_LVAR_EQUAL_TO_NUMBER 60@ 1",
    0xBD1DE: "LOCATE_CHAR_ON_FOOT_3D",
    0xBD264: "IS_BUTTON_PRESSED 0 15",
    0xBD666: "WAIT 0",
    0xBD67A: "DELETE_OBJECT 117(36@,7)",
}
for offset, expected in checks.items():
    assert expected in lines[offset], lines[offset]
    print(lines[offset])
assert source.data[0xBAD5A:0xBAD62] == bytes.fromhex("90 00 02 C4 68 03 A7 00")
assert "GOTO_IF_FALSE -23696" in lines[0xBA9DA]
assert start + 23696 == 0xBC630  # Outside: next iteration skips the noise routine.
assert "GOTO_IF_FALSE -17507" in lines[0xBAB06]
assert start + 17507 == 0xBAE03  # House-only native noise sample/comparison.
print("PASS: original hash, native 0090 insertion, outside gates and crate lifecycle")
print("Evidence only; does not execute GTA, test outfits or implement crate transport.")
