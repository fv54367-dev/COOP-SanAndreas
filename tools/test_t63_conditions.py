"""Exercise production T63 probe/routing functions with a fake game/network, without GTA.

Requires g++; generated sources/executables stay in build/t63_unit. This verifies
context preservation and authorization, not native GTA handlers or animations.
"""
from pathlib import Path
import shutil
import subprocess
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/t63_unit'

# Optional local evidence: never write the installed SCM, and never require it in CI.
original = ROOT.parent / 'GTA_JUGADOR1/data/script/main.scm'
if original.exists():
    data = original.read_bytes()
    source = (ROOT / 'client/src/CMissionGuestCounts.cpp').read_text(encoding='utf-8')
    spots = re.findall(r'\{"\w+", (0x[\dA-F]+), (0x[\dA-F]+), (\d+), '
                       r'(0x[\dA-F]+), (0x[\dA-F]+), \d+, \d+\}', source)
    assert len(spots) == 4
    for start, at, length, expected, opcode in spots:
        start, at, expected, opcode = (int(v, 16) for v in (start, at, expected, opcode))
        h = 2166136261
        for value in data[start:start + int(length)]:
            h = ((h ^ value) * 16777619) & 0xffffffff
        assert h == expected, hex(at)
        assert struct.unpack_from('<H', data, at)[0] & 0x7fff == opcode
    print('PASS original SCM: four offsets/opcodes and full mission fingerprints', flush=True)


def function(path, start, end):
    text = (ROOT / path).read_text(encoding='utf-8')
    return text[text.index(start):text.index(end, text.index(start))]


def run(name, source):
    OUT.mkdir(parents=True, exist_ok=True)
    cpp, exe = OUT / (name + '.cpp'), OUT / (name + '.exe')
    cpp.write_text(source, encoding='utf-8')
    compiler = shutil.which('g++')
    if not compiler:
        raise RuntimeError('g++ is required')
    subprocess.run([compiler, '-std=c++17', '-O0', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


probe = function('client/src/COpCodeSync.cpp', 'bool COpCodeSync::ProbeCondition(',
                 'static void CollectParamsProperly()')
run('probe', r'''
#include <cassert>
#include <initializer_list>
#include <cstdint>
#include <cstring>
#include <cstdio>
constexpr int NUM_SYNCED_PARAMS = 15;
struct OpcodeParameter { int value; };
int ScriptParams[32];
uint16_t scriptParamCount = 3, argParamCount = 2;
struct CRunningScript {
    unsigned char* m_pCurrentIP;
    bool m_bNotFlag, m_bCondResult;
    unsigned short m_nLogicalOp;
    int locals[32];
    static inline void (*CommandHandlerTable[27])(CRunningScript*, int);
};
struct COpCodeSync {
    static inline OpcodeParameter scriptParamsBuffer[NUM_SYNCED_PARAMS];
    static bool ProbeCondition(CRunningScript*, uint16_t, const int*, int);
};
int calls = 0;
void handler(CRunningScript* script, int op) {
    ++calls;
    assert(!script->m_bNotFlag && !script->m_nLogicalOp);
    assert(script->m_pCurrentIP[0] == 1);
    int actor; memcpy(&actor, script->m_pCurrentIP + 1, 4);
    script->m_bCondResult = actor == 7;
    script->m_pCurrentIP += 5;
    script->locals[0] = 99;
    // The real CollectParameters hook also changes this shared state.
    for (auto& p : ScriptParams) p = -1;
    for (auto& p : COpCodeSync::scriptParamsBuffer) p.value = -1;
    scriptParamCount = argParamCount = 15;
}
''' + probe + r'''
int main() {
    for (auto& h : CRunningScript::CommandHandlerTable) h = handler;
    for (auto& p : ScriptParams) p = 42;
    for (auto& p : COpCodeSync::scriptParamsBuffer) p.value = 123;
    for (bool negate : {false, true}) for (int logical : {0, 1, 8, 21, 28}) {
        unsigned char ip[8]{};
        CRunningScript source{};
        source.m_pCurrentIP = ip; source.m_bNotFlag = negate;
        source.m_nLogicalOp = logical; source.m_bCondResult = true;
        const auto saved = source;
        for (int actor : {7, 8}) {
            assert(COpCodeSync::ProbeCondition(&source, 0x02E0, &actor, 1) == (actor == 7));
            assert(memcmp(&source, &saved, sizeof(source)) == 0);
            assert(scriptParamCount == 3 && argParamCount == 2);
            for (auto p : ScriptParams) assert(p == 42);
            for (auto p : COpCodeSync::scriptParamsBuffer) assert(p.value == 123);
        }
    }
    int p = 7, before = calls;
    assert(!COpCodeSync::ProbeCondition(nullptr, 0x02E0, &p, 1));
    assert(!COpCodeSync::ProbeCondition((CRunningScript*)1, 0x00A1, &p, 1));
    assert(!COpCodeSync::ProbeCondition((CRunningScript*)1, 0x02E0, &p, 9));
    assert(calls == before);
    puts("PASS probe: 20 NOT/AND/OR contexts, IP/locals/parameters/sync restored, whitelist");
}
''')

routing = (ROOT / 'server/src/PacketHandlers/MissionGuestCounts.cpp').read_text(encoding='utf-8')
routing = routing[routing.index('PACKET_HANDLER('):]
run('routing', r'''
#include <cassert>
#include <vector>
#include <cstdio>
using playerid_t = int;
struct CNetworkPlayer {
    bool m_bIndividualMode = false, included = true, authority = false, m_bOnMission = false;
    int m_playerId = 0;
};
namespace Packets::Scripts { struct MissionGuestCounts {
    int epoch = 1, mission = 15, senderPlayerid = 999, owner = 2;
    bool request = false;
}; }
struct CNetworkPlayerManager {
    static inline std::vector<CNetworkPlayer*> m_pPlayers;
    static CNetworkPlayer* GetPlayer(int id) {
        for (auto p : m_pPlayers) if (p->m_playerId == id) return p;
        return nullptr;
    }
};
struct CCoopMission {
    static bool CanReceiveMissionPacket(CNetworkPlayer* p) { return p->included; }
    static bool IsScriptAuthority(CNetworkPlayer* p) { return p->authority; }
};
std::vector<int> recipients;
struct Factory { void Send(Packets::Scripts::MissionGuestCounts&, CNetworkPlayer* p) {
    recipients.push_back(p->m_playerId);
} } factory;
Factory& GetPacketFactory() { return factory; }
#define PACKET_HANDLER(kind, ...) void Handle(__VA_ARGS__)
''' + routing + r'''
int main() {
    CNetworkPlayer host, owner, guest, fourth;
    host.m_playerId=1; owner.m_playerId=2; guest.m_playerId=3; fourth.m_playerId=4;
    owner.authority=true; owner.m_bOnMission=true;
    CNetworkPlayerManager::m_pPlayers={&host,&owner,&guest,&fourth};
    Packets::Scripts::MissionGuestCounts p;
    p.request=true; p.owner=999;
    Handle(&p,&owner);
    assert((recipients==std::vector<int>{1,3,4}));
    assert(p.owner==2 && p.senderPlayerid==2);
    recipients.clear(); Handle(&p,&host); assert(recipients.empty());
    p.request=false; p.owner=2; Handle(&p,&guest);
    assert((recipients==std::vector<int>{2}) && p.senderPlayerid==3);
    recipients.clear(); guest.m_bIndividualMode=true; Handle(&p,&guest); assert(recipients.empty());
    guest.m_bIndividualMode=false; guest.included=false; Handle(&p,&guest); assert(recipients.empty());
    guest.included=true; owner.m_bOnMission=false; Handle(&p,&guest); assert(recipients.empty());
    owner.m_bOnMission=true; owner.m_bIndividualMode=true; Handle(&p,&guest); assert(recipients.empty());
    owner.m_bIndividualMode=false; p.owner=12345; Handle(&p,&guest); assert(recipients.empty());
    p.owner=2; p.epoch=0; Handle(&p,&guest); assert(recipients.empty());
    p.epoch=1; p.mission=-1; Handle(&p,&guest); assert(recipients.empty());
    p.mission=15; p.request=true; guest.included=false; fourth.m_bIndividualMode=true;
    Handle(&p,&owner); assert((recipients==std::vector<int>{1}));
    puts("PASS routing: four players, owner != host, sender sealed, individual/absent/ended/invalid rejected");
}
''')
