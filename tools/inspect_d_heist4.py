"""Read-only evidence for D (Cop Wheels, HEIST4): fingerprint of the mission and the two bike-load conditions."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path

root = Path(__file__).resolve().parents[1]
data = (root / 'scm' / 'main.scm').read_bytes()
start, size = 0x246B1D, 13637  # HEIST4 in main.scm (scm_missions.py of sa-coop-karimov: index 99)
fingerprint = 2166136261
for byte in data[start:start + size]:
    fingerprint = ((fingerprint ^ byte) * 16777619) & 0xFFFFFFFF
assert fingerprint == 0x68971E7B, f"Unsupported original: {fingerprint:08X}"
# HEIST4.txt 876: Char.IsInCar($scplayer, 279@(48@,4i)) -> opcode 00DB at mission offset 5939
assert data[0x248250:0x248252] == bytes.fromhex("db00")
assert 0x246B1D + 5939 == 0x248250
# HEIST4.txt 931: Char.IsInAngledAreaInCar3D($scplayer, ...) -> opcode 05FE at mission offset 6340 (9 parameters)
assert data[0x2483E1:0x2483E3] == bytes.fromhex("fe05")
assert 0x246B1D + 6340 == 0x2483E1
# HEIST4.txt 943: Car.AttachToCar(279@(48@,4i), 244@, ...) -> opcode 0683 at offset 6455
assert data[0x246B1D + 6455:0x246B1D + 6457] == bytes.fromhex("8306")
print("PASS: HEIST4 fingerprint, IS_CHAR_IN_CAR 00DB, IN_ANGLED_AREA_IN_CAR_3D 05FE, ATTACH_CAR_TO_CAR 0683")
