"""Compile the real mirror policy and exercise object resolution transitions."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build/t64_unit'
out.mkdir(parents=True, exist_ok=True)
source = out / 'blips.cpp'
source.write_text(r'''
#include <cassert>
#include <cstdio>
#include "CMirrorBlipsPolicy.h"
int main() {
    using namespace MirrorBlipsPolicy;
    SBlipState box;
    box.type = Type::OBJECT;
    box.display = Display::BOTH;
    box.entityKind = eEntityKind::OBJECT;
    box.entityId = 7;
    auto pending = DecideLocalForm(box, false, false);
    auto ready = DecideLocalForm(box, true, false);
    assert(pending == eLocalForm::FOLLOW);
    assert(LocalDisplay(box, pending) == Display::BLIP_ONLY);
    assert(ready == eLocalForm::ENTITY);
    assert(LocalDisplay(box, ready) == Display::BOTH);
    assert(LocalType(box, ready) == Type::OBJECT);
    assert(NeedsRebuild(true, pending, Type::COORD, 0, ready, Type::OBJECT, 19));
    assert(NeedsRebuild(true, ready, Type::OBJECT, 19, ready, Type::OBJECT, 20));
    assert(!NeedsRebuild(true, ready, Type::OBJECT, 19, ready, Type::OBJECT, 19));
    assert(NeedsRebuild(true, ready, Type::OBJECT, 19, pending, Type::COORD, 0));
    box.entityKind = eEntityKind::NONE;
    assert(DecideLocalForm(box, false, false) == eLocalForm::FOLLOW);
    box.entityKind = eEntityKind::PLAYER;
    assert(DecideLocalForm(box, true, true) == eLocalForm::NONE);
    box.entityKind = eEntityKind::VEHICLE;
    assert(LocalType(box, ready) == Type::CAR);
    puts("PASS: object identity, fallback, native marker, pool reuse, removal, previous kinds");
}
''', encoding='utf-8')
compiler = shutil.which('g++')
if not compiler:
    raise RuntimeError('g++ required')
exe = out / 'blips.exe'
subprocess.run([compiler, '-std=c++17', '-I' + str(root / 'client/src'), str(source), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
