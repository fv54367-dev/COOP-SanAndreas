"""Read-only original-script evidence for the T71 allowlist."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import inspect_t64 as source

truck = bytes.fromhex('FF 00 02 0C 00 03 9A 00 03 9B 00 06 00 00 C0 41 06 00 00 C0 3F 06 00 00 C0 3F 06 00 00 00 40 04 00')
assert source.data[0xBD1DE:0xBD1DE + len(truck)] == truck
lines = dict(source.out)
for address in (0xBAA62, 0xBAD13, 0xBD119, 0xBD264, 0xBD666, 0xBD67A):
    print(lines[address])
print('PASS: T71 original truck signature and lifecycle sites (evidence, not game test)')

# Execute the production receiver with fake native state to exercise arbitration.
import shutil
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = (root / 'client/src/CHomeInvasion.cpp').read_text(encoding='utf-8')
receive = cpp[cpp.index('void CHomeInvasion::Receive('):cpp.index('bool CHomeInvasion::IsCarried(')]
fake = r'''
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>
#include <cstdio>
using playerid_t = int; using DWORD = unsigned;
struct Vec { float x=0; Vec operator-(Vec v)const{return {x-v.x};} float Magnitude()const{return x<0?-x:x;} };
struct Ped { int m_nAreaCode=8; struct { bool bInVehicle=false; } m_nPedFlags; Vec pos; Vec GetPosition(){return pos;} } me;
struct CObject { struct { bool bIsLiftable=true; } m_nObjectFlags; int m_nAreaCode=8; Vec pos; Vec GetPosition(){return pos;} } box;
struct P { int senderPlayerid=0,owner=2,holder=0; unsigned epoch=5,sequence=1,crate=0,crates[7]={7};
 unsigned liftable=1; unsigned noiseTotal=0; bool snapshot=false,active=true,holding=false,enter=false,request=false; } session;
struct CNetworkPlayer { int m_playerId=0,m_nRunningMission=24; Ped* m_pPed=&me; bool eligible=true,owner=false; } owner,guest,third;
struct CNetworkPlayerManager {
 static inline std::vector<CNetworkPlayer*> m_pPlayers;
 static CNetworkPlayer* GetPlayer(int id){for(auto p:m_pPlayers)if(p->m_playerId==id)return p;return nullptr;}
};
struct CCoopMission { static inline bool authority=true; static bool IsScriptAuthority(){return authority;} };
struct COpCodeSync { static bool IsLocalMissionRunning(){return false;} };
struct Local { int iParam=0; };
struct CTheScripts { static inline Local LocalVariablesForCurrentMission[200]; };
struct CHomeInvasion { static void Receive(const P&); };
struct CMissionMirror { template<class... T> static void LogEventf(const char*,T...){} };
struct Input { unsigned sequence=0,at=0; unsigned noiseTotal=0; float pendingNoise=0; bool holding=false; unsigned enterAt=0; };
std::map<int,Input> inputs;
bool enabled=true; unsigned now=1000,heard=0; int grants=0,cleans=0; int script;
int* active=&script;
uint32_t noiseTotal=0; bool House(Ped* p){return p->m_nAreaCode==8;}
unsigned GetTickCount(){return now;}
bool Enabled(){return enabled;} bool Eligible(CNetworkPlayer* p){return p&&p->eligible;}
bool Owner(CNetworkPlayer* p){return p&&p->owner;} bool HouseStage(){return true;}
void CleanGuest(){++cleans;} void Release(){} void Follow(){} void AnnounceCrate(){++grants;}
CObject* Object(unsigned id){return id==7?&box:nullptr;}
Ped* FindPlayerPed(int){return &me;}
void* pending=nullptr; void* Taking(Ped*){return pending;}
'''
# The production log uses the strongly typed player id; emulate only that expression.
receive = receive.replace('session.holder.value()', 'session.holder')
tests = r'''
int main(){
 owner.m_playerId=2;owner.owner=true;guest.m_playerId=3;third.m_playerId=4;
 CNetworkPlayerManager::m_pPlayers={&owner,&guest,&third};
 CTheScripts::LocalVariablesForCurrentMission[94].iParam=1;
 P p;p.senderPlayerid=3;p.request=true;p.crate=7;
 CHomeInvasion::Receive(p);assert(session.crate==7&&session.holder==3&&grants==1);
 p.senderPlayerid=4;CHomeInvasion::Receive(p);assert(session.holder==3&&grants==1);
 p.senderPlayerid=3;p.enter=true;p.holding=true;p.request=false;p.sequence=2;
 CHomeInvasion::Receive(p);assert(inputs[3].enterAt==now);
 now+=50;CHomeInvasion::Receive(p);assert(inputs[3].enterAt==now-50); // replay
 p.sequence=3;p.epoch=4;CHomeInvasion::Receive(p);assert(inputs[3].sequence==2);
 p.epoch=5;guest.eligible=false;CHomeInvasion::Receive(p);assert(inputs[3].sequence==2);guest.eligible=true;
 session.crate=0;session.holder=0;p.request=true;p.sequence=4;pending=&box;
 CHomeInvasion::Receive(p);assert(!session.crate);pending=nullptr;
 p.sequence=5;me.m_nAreaCode=0;CHomeInvasion::Receive(p);assert(!session.crate);me.m_nAreaCode=8;
 p.sequence=6;p.crate=999;CHomeInvasion::Receive(p);assert(!session.crate);
 p.sequence=7;p.crate=7;me.pos.x=10;CHomeInvasion::Receive(p);assert(!session.crate);me.pos.x=0;
 p.sequence=8;CHomeInvasion::Receive(p);assert(session.holder==3&&grants==2);
 p.request=false;p.sequence=9;p.noiseTotal=12500;CHomeInvasion::Receive(p);
 assert(inputs[3].pendingNoise==12.5f);
 CHomeInvasion::Receive(p);assert(inputs[3].pendingNoise==12.5f);
 p.sequence=11;p.noiseTotal=25000;CHomeInvasion::Receive(p);assert(inputs[3].pendingNoise==25);
 p.senderPlayerid=4;p.sequence=2;p.noiseTotal=10000;CHomeInvasion::Receive(p);assert(inputs[4].pendingNoise==10);
 CTheScripts::LocalVariablesForCurrentMission[94].iParam=0;
 p.sequence=3;p.noiseTotal=20000;CHomeInvasion::Receive(p);assert(inputs[4].pendingNoise==0);
 CTheScripts::LocalVariablesForCurrentMission[94].iParam=1;
 p.sequence=4;p.noiseTotal=21000;CHomeInvasion::Receive(p);assert(inputs[4].pendingNoise==1);
 guest.eligible=false;p.senderPlayerid=3;p.sequence=12;p.noiseTotal=30000;
 CHomeInvasion::Receive(p);assert(inputs[3].pendingNoise==25);guest.eligible=true;
 CCoopMission::authority=false;p=P{};p.snapshot=true;p.senderPlayerid=4;p.sequence=100;
 CHomeInvasion::Receive(p);assert(session.sequence==1); // guest cannot forge confirmation
 p.senderPlayerid=2;p.epoch=6;p.sequence=1;p.holder=4;p.crate=7;
 CHomeInvasion::Receive(p);assert(session.holder==4&&session.epoch==6);
 p.epoch=5;p.sequence=100;CHomeInvasion::Receive(p);assert(session.epoch==6);
 puts("PASS T74 production receiver: noise sums, replay/loss, outside, excluded, first grant, contenders, owner pickup, replay, epoch, F1, area, distance, forged confirmation");
}
'''
out = root / 'build/t74_unit'
out.mkdir(parents=True, exist_ok=True)
(out / 'receive.cpp').write_text(fake + receive + tests, encoding='utf-8')
subprocess.run([shutil.which('g++'), '-std=c++17', str(out / 'receive.cpp'), '-o', str(out / 'receive.exe')], check=True)
subprocess.run([str(out / 'receive.exe')], check=True)

body = cpp[cpp.index("    // Add each guest's unconsumed"):cpp.index('\n}\nvoid CHomeInvasion::Process()')]
body = body.replace('player->m_pPed', 'player->ped')
harness = r'''
#include <algorithm>
#include <cassert>
#include <map>
#include <cstdint>
struct Ped{} ped;
struct Player { Ped* ped; bool eligible=true; } players[4]={{&ped},{&ped},{&ped},{&ped}};
struct CNetworkPlayerManager { static Player* GetPlayer(int id){return &players[id];} };
struct Local { float fParam=0; int iParam=0; };
struct CTheScripts { static inline Local LocalVariablesForCurrentMission[200]; static inline char ScriptSpace[30000]; };
struct Input { unsigned at=100; float pendingNoise=0; };
std::map<int,Input> inputs;
constexpr unsigned noiseVar=26820;
unsigned now=100;
unsigned GetTickCount(){return now;}
bool Eligible(Player* p){return p->eligible;}
bool House(Ped*){return true;}
void Apply(){
''' + body + r'''
}
int main(){
 auto& value=CTheScripts::LocalVariablesForCurrentMission[167].fParam;
 CTheScripts::LocalVariablesForCurrentMission[94].iParam=1;
 value=10;inputs[1].pendingNoise=20;inputs[2].pendingNoise=30;inputs[3].pendingNoise=40;
 Apply();assert(value>99&&value<100);assert(*reinterpret_cast<int*>(&CTheScripts::ScriptSpace[noiseVar])==99);
 auto before=value;Apply();assert(value==before);
 inputs[1].pendingNoise=10;Apply();assert(value>=100); // native BAD66 will wake colonel
 value=0;inputs[1].pendingNoise=10;CTheScripts::LocalVariablesForCurrentMission[94].iParam=0;
 Apply();assert(value==0&&inputs[1].pendingNoise==0);
 CTheScripts::LocalVariablesForCurrentMission[94].iParam=1;
 inputs[1].pendingNoise=10;now=800;Apply();assert(value==0);
 inputs[1].at=now;inputs[1].pendingNoise=10;players[1].eligible=false;Apply();assert(value==0);
}
'''
(out / 'accumulator.cpp').write_text(harness, encoding='utf-8')
subprocess.run([shutil.which('g++'), '-std=c++17', str(out / 'accumulator.cpp'), '-o', str(out / 'accumulator.exe')], check=True)
subprocess.run([str(out / 'accumulator.exe')], check=True)
print('PASS T74 native accumulator: 3 guests, one-shot consumption, threshold, 94@=0, stale, excluded')
