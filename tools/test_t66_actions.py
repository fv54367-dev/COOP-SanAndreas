"""Execute production T66 functions with fake GTA/network state; no game visuals."""
from pathlib import Path
import subprocess
import shutil

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/t66_unit'
OUT.mkdir(parents=True, exist_ok=True)


def run(name, source):
    cpp, exe = OUT / (name + '.cpp'), OUT / (name + '.exe')
    cpp.write_text(source, encoding='utf-8')
    subprocess.run([shutil.which('g++'), '-std=c++17', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def section(start, end):
    source = (ROOT / 'client/src/CMissionGuestActions.cpp').read_text(encoding='utf-8')
    return source[source.index(start):source.index(end, source.index(start))]


route = (ROOT / 'server/src/PacketHandlers/MissionGuestActions.cpp').read_text(encoding='utf-8')
route = route[route.index('PACKET_HANDLER('):]
run('routing', r'''
#include <cassert>
#include <vector>
#include <cstdio>
struct CNetworkPlayer { int m_playerId=0; bool m_bIndividualMode=false,included=true,m_bOnMission=false,authority=false; };
namespace Packets::Scripts { struct MissionGuestActions {
 enum {SESSION,INPUT,BEGIN,END,THROW,READY}; int epoch=1,mission=42,kind=SESSION,owner=999,actor=3,senderPlayerid=999;
}; }
struct CNetworkPlayerManager {
 static inline std::vector<CNetworkPlayer*> m_pPlayers;
 static CNetworkPlayer* GetPlayer(int id) { for(auto p:m_pPlayers) if(p->m_playerId==id)return p;return nullptr; }
};
struct CCoopMission {
 static bool CanReceiveMissionPacket(CNetworkPlayer* p){return p->included;}
 static bool IsScriptAuthority(CNetworkPlayer* p){return p->authority;}
};
std::vector<int> recipients;
struct Factory { void Send(Packets::Scripts::MissionGuestActions&,CNetworkPlayer* p){recipients.push_back(p->m_playerId);} } factory;
Factory& GetPacketFactory(){return factory;}
#define PACKET_HANDLER(kind,...) void Handle(__VA_ARGS__)
''' + route + r'''
int main(){
 CNetworkPlayer host,owner,guest,fourth;
 host.m_playerId=1;owner.m_playerId=2;guest.m_playerId=3;fourth.m_playerId=4;
 owner.authority=true;owner.m_bOnMission=true;
 CNetworkPlayerManager::m_pPlayers={&host,&owner,&guest,&fourth};
 using P=Packets::Scripts::MissionGuestActions; P p;
 Handle(&p,&owner);assert((recipients==std::vector<int>{1,3,4}));assert(p.owner==2&&p.senderPlayerid==2);
 for(int kind:{P::BEGIN,P::END}){
  recipients.clear();p.kind=kind;Handle(&p,&owner);assert(recipients.size()==3);
  recipients.clear();Handle(&p,&guest);assert(recipients.empty());
  p.actor=999;Handle(&p,&owner);assert(recipients.empty());p.actor=3;
 }
 for(int kind:{P::INPUT,P::THROW,P::READY}){
  p.kind=kind;recipients.clear();p.actor=999;Handle(&p,&guest);assert((recipients==std::vector<int>{2}));assert(p.actor==3&&p.senderPlayerid==3);
  recipients.clear();guest.m_bIndividualMode=true;Handle(&p,&guest);assert(recipients.empty());guest.m_bIndividualMode=false;
  guest.included=false;Handle(&p,&guest);assert(recipients.empty());guest.included=true;
  owner.m_bOnMission=false;Handle(&p,&guest);assert(recipients.empty());owner.m_bOnMission=true;
  p.owner=1;Handle(&p,&guest);assert(recipients.empty());p.owner=2;
  p.epoch=0;Handle(&p,&guest);assert(recipients.empty());p.epoch=1;
  p.mission=-1;Handle(&p,&guest);assert(recipients.empty());p.mission=42;
 }
 recipients.clear();p.kind=P::SESSION;guest.included=false;fourth.m_bIndividualMode=true;Handle(&p,&owner);assert((recipients==std::vector<int>{1}));
 puts("PASS T66 routing: four players, non-host owner, sealed identities, all six kinds, F1/leave/end/invalid");
}
''')

common = r'''
#include <cassert>
#include <map>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <cstdint>
struct Id {int n=0;Id(int v=0):n(v){} int value()const{return n;} operator int()const{return n;} };
using playerid_t=Id;using DWORD=unsigned;
struct P {
 enum {SESSION,INPUT,BEGIN,END,THROW,READY}; Id actor,owner; unsigned sequence=0,epoch=1;int action=1,mission=42,kind=SESSION;
 bool pressed=false,crouched=false;float x=0,y=0,z=0;
};
struct CRunningScript {bool m_bIsMission=true;char m_szName[9]="HEIST2";} script;
struct Ped {int ref;unsigned char m_nAreaCode=17;} me{1},guest{3},fourth{4};
struct CNetworkPlayer {Ped* m_pPed;bool eligible=true;} remote{&guest},remote4{&fourth};
struct CNetworkPlayerManager {
 static CNetworkPlayer* GetPlayer(Id i){return i==3?&remote:i==4?&remote4:nullptr;}
};
struct Object{int m_nObjectType=0;unsigned char m_nAreaCode=0;} object;
struct CPools { static int GetPedRef(Ped*p){return p->ref;} static Object* GetObject(int id){return id==123?&object:nullptr;} };
Ped* FindPlayerPed(int){return &me;}
bool enabled=true,authority=true;unsigned now=1000;
DWORD GetTickCount(){return now;}
bool Enabled(){return enabled;}
bool Eligible(CNetworkPlayer*p){return p&&p->eligible;}
struct CCoopMission{static bool IsScriptAuthority(){return authority;}};
int params[32],locals[1024];int* ScriptParams=params;
float coords[1024];int& L(int i){return locals[i];}float& F(int i){return coords[i];}
struct Spec {int action;unsigned locate,button;} heist{1,0x23B69E,0x23B6FF},wuzi{2,0x16ED89,0x16EDDB};
Spec* spec=&heist;CRunningScript* active=&script;unsigned ip=0x23B69E,sequence=0;
bool At(CRunningScript*,unsigned off){return ip==off;}
P session;
struct Input {P packet;unsigned at=1000,consumed=0;};std::map<Id,Input> inputs;
struct Action{P packet;unsigned at=0;int slot=-1;bool ready=false;} action;
int probes=0;
struct COpCodeSync {
 static inline bool bProcessingNetworkOpcode=false;
 static bool ProbeCondition(CRunningScript*,uint16_t op,const int*p,int n){++probes;assert(op==0x00FF&&n==8&&p[7]==0);return p[0]==3||p[0]==4;}
};
int sends=0,begins=0,cancels=0,creates=0,blips=0;
struct Factory{void Send(P){++sends;}}factory;Factory& GetPacketFactory(){return factory;}
void BeginAnimation(P){++begins;}
struct CMissionMirror {template<class...T>static void LogEventf(const char*,T...){} };
struct CMissionGuestActions{static bool FilterCompare(CRunningScript*,uint16_t,bool,bool&);};
struct NativeScope{};
Ped* Actor(Id id){return id==3&&remote.eligible?&guest:id==4&&remote4.eligible?&fourth:nullptr;}
void Cancel(bool release){if(release&&action.slot>=0)L(108+action.slot)=0;++cancels;action=Action{};}
enum {OBJECT_MISSION=2,MISSION_CLEANUP_ENTITY_TYPE_OBJECT=3};
struct Cleanup{bool free=true;int added=0;bool FindFree(){return free;}void AddEntityToList(int h,int t){assert(h==123&&t==3);++added;}}cleanup;
struct CTheScripts{static inline Cleanup& MissionCleanUp=cleanup;};
namespace Commands{enum{GET_OFFSET_FROM_CHAR_IN_WORLD_COORDS,CREATE_OBJECT,SET_OBJECT_HEADING,REMOVE_BLIP};}
namespace plugin{
 template<int op,class...T>void Command(T...args){
  auto t=std::make_tuple(args...);
  if constexpr(op==Commands::GET_OFFSET_FROM_CHAR_IN_WORLD_COORDS){assert(std::get<0>(t)==3);*std::get<4>(t)=10;*std::get<5>(t)=20;*std::get<6>(t)=30;}
  if constexpr(op==Commands::CREATE_OBJECT){assert(std::get<0>(t)==363);*std::get<4>(t)=123;++creates;}
  if constexpr(op==Commands::REMOVE_BLIP){assert(std::get<0>(t)==77);++blips;}
 }
}
'''
run('placements', common + section('void FinishCharge()', 'bool OnTrain(') +
    section('bool CMissionGuestActions::FilterCompare(', 'void CMissionGuestActions::Receive(') + r'''
int main(){
 bool handled=false;L(37)=2;L(140+2)=77;params[0]=1;
 Input in;in.packet.pressed=true;in.packet.sequence=1;inputs[3]=in;inputs[4]=in;
 auto compare=[&](bool native=false){return CMissionGuestActions::FilterCompare(&script,0x00FF,native,handled);};
 assert(compare(true)&&handled&&begins==0&&L(34)==0);
 inputs[3].at=inputs[4].at=1;assert(!compare()&&begins==0);inputs[3].at=inputs[4].at=1000;
 remote.eligible=remote4.eligible=false;assert(!compare()&&begins==0);remote.eligible=remote4.eligible=true;
 enabled=false;handled=false;assert(!compare()&&!handled);enabled=true;
 assert(!compare()&&handled&&begins==1&&sends==1&&L(110)==1&&L(34)==0&&action.packet.actor==3);
 assert(!compare()&&begins==1);
 FinishCharge();assert(L(34)==1&&L(110)==1&&L(171)==123&&creates==1&&blips==1&&cleanup.added==1);
 assert(object.m_nObjectType==OBJECT_MISSION&&object.m_nAreaCode==17);
 FinishCharge();assert(L(34)==1&&creates==1);
 action.slot=3;action.packet.actor=3;action.packet.sequence=2;L(111)=1;cleanup.free=false;
 FinishCharge();assert(L(111)==0&&L(34)==1&&creates==1);
 cleanup.free=true;action.slot=3;action.packet.actor=3;action.packet.sequence=3;L(111)=1;remote.eligible=false;
 FinishCharge();assert(L(111)==0&&L(34)==1);remote.eligible=true;
 spec=&wuzi;session.action=2;ip=wuzi.locate;params[0]=1;L(48)=0;L(522)=0;inputs[3].packet.sequence=2;
 assert(!compare()&&action.packet.action==2&&action.packet.sequence);
 assert(!action.ready);action.ready=true;
 assert(compare());ip=wuzi.button;
 assert(CMissionGuestActions::FilterCompare(&script,0x00E1,false,handled)&&handled);
 ip=123;handled=false;assert(!CMissionGuestActions::FilterCompare(&script,0x00E1,false,handled)&&!handled);
 puts("PASS T66 placements: owner priority, stale/F1/area rejection, deduplication, one-time count, native object cleanup, cancellation, exact WUZI2 button");
}
''')
print('T66 isolated tests passed; native GTA and visuals remain untested.')

run('throws', r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <tuple>
struct Id {int n=3;int value()const{return n;}};
struct CVector {
 float x=0,y=0,z=0;
 CVector(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
 CVector operator+(CVector v)const{return {x+v.x,y+v.y,z+v.z};}
 CVector operator-(CVector v)const{return {x-v.x,y-v.y,z-v.z};}
 CVector operator*(float v)const{return {x*v,y*v,z*v};}
 float Magnitude()const{return std::sqrt(x*x+y*y+z*z);}
};
struct Matrix {CVector right{1,0,0},up{0,1,0};} matrix;
struct CPed {
 struct {bool bInVehicle=false;}m_nPedFlags;
 int m_nAreaCode=0;void* m_pAttachedTo=(void*)1;CVector pos{0,-5,2};
 CVector GetPosition(){return pos;}
} owner,guest;
struct CVehicle {
 int m_nAreaCode=0;CVector m_vecMoveSpeed{0.2f,0,0},pos;
 CVector GetPosition(){return pos;} Matrix* GetMatrix(){return &matrix;}
} train;
struct Object{int m_nObjectType=0,m_nAreaCode=0;} object;
int locals[1024],created=0,deleted=0,configured=0;CVector velocity;
int& L(int i){return locals[i];}
struct CPools {
 static CVehicle* GetVehicle(int h){return h==7?&train:nullptr;}
 static Object* GetObject(int h){return h==123?&object:nullptr;}
};
struct Cleanup {
 bool free=true;int added=0,removed=0;
 bool FindFree(){return free;}
 void AddEntityToList(int,int){++added;}void RemoveEntityFromList(int,int){++removed;}
}cleanup;
struct CTheScripts{static inline Cleanup& MissionCleanUp=cleanup;};
CPed* Actor(Id){return &guest;} CPed* FindPlayerPed(int){return &owner;}
struct Spec{int action=0;}entry;Spec* spec=&entry;void* active=(void*)1;
struct P{Id actor;float power=60,x=0,y=-5,z=3,dx=0,dy=1,dz=0;};
struct NativeScope{};
enum{OBJECT_MISSION=2,MISSION_CLEANUP_ENTITY_TYPE_OBJECT=3};
namespace Commands {enum{DELETE_OBJECT,CREATE_OBJECT_NO_OFFSET,SET_OBJECT_RECORDS_COLLISIONS,SET_OBJECT_DYNAMIC,SET_OBJECT_COLLISION,SET_OBJECT_VELOCITY};}
namespace plugin {
 template<int op,class...T>void Command(T...args){auto t=std::make_tuple(args...);
  if constexpr(op==Commands::CREATE_OBJECT_NO_OFFSET){assert(std::get<0>(t)==-151);*std::get<4>(t)=123;++created;}
  if constexpr(op==Commands::DELETE_OBJECT){++deleted;}
  if constexpr(op==Commands::SET_OBJECT_VELOCITY){velocity={std::get<1>(t),std::get<2>(t),std::get<3>(t)};}
  if constexpr(op==Commands::SET_OBJECT_DYNAMIC||op==Commands::SET_OBJECT_COLLISION||op==Commands::SET_OBJECT_RECORDS_COLLISIONS){++configured;}
 }
}
struct CMissionMirror{template<class...T>static void LogEventf(const char*,T...){} };
''' + section('bool OnTrain(', 'void HostThrow(') +
    section('void HostThrow(', '\n}\n\nvoid CMissionGuestActions::OnCommandStart') + r'''
int main(){
 L(89)=10;L(163)=7;L(127)=-151;P p;
 HostThrow(p);assert(created==1&&L(110)==123&&L(166)==-151&&L(109)==1);
 assert(configured==3&&cleanup.added==1&&object.m_nObjectType==OBJECT_MISSION);
 assert(std::abs(velocity.x-10)<0.001&&std::abs(velocity.y-30)<0.001&&velocity.z==0);
 p.power=20;HostThrow(p);p.power=NAN;HostThrow(p);p.power=101;HostThrow(p);p.power=60;
 p.dy=100;HostThrow(p);p.dy=1;p.x=99;HostThrow(p);p.x=0;
 guest.pos.x=20;HostThrow(p);guest.pos.x=0;guest.m_nAreaCode=1;HostThrow(p);guest.m_nAreaCode=0;
 owner.m_pAttachedTo=nullptr;HostThrow(p);owner.m_pAttachedTo=(void*)1;
 L(89)=9;HostThrow(p);L(89)=10;assert(created==1&&L(109)==1);
 L(109)=9;HostThrow(p);assert(created==2&&L(109)==0&&L(119)==123);
 HostThrow(p);assert(created==3&&deleted==1&&cleanup.removed==1);
 cleanup.free=false;HostThrow(p);assert(created==3);
 puts("PASS T66 Catalyst: original slots/model/index, native velocity units, cleanup, power/camera/train/area/stage validation");
}
''')

# Exercise the production nested capture guard: helpers can call other helpers.
run('capture', r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
static uint16_t argParamCount = 7;
static unsigned localCommandDepth = 0;
static uint16_t savedLocalArgCount = 0;
struct COpCodeSync { static void BeginLocalCommands(); static void EndLocalCommands(); };
""" + (ROOT/'client/src/COpCodeSync.cpp').read_text(encoding='utf-8').split('void COpCodeSync::BeginLocalCommands()',1)[1].split('bool COpCodeSync::ProbeCondition(',1)[0].join(['void COpCodeSync::BeginLocalCommands()', '']) + r"""
int main() {
 COpCodeSync::BeginLocalCommands(); argParamCount=11;
 COpCodeSync::BeginLocalCommands(); argParamCount=15;
 COpCodeSync::EndLocalCommands(); assert(localCommandDepth==1);
 COpCodeSync::EndLocalCommands(); assert(localCommandDepth==0 && argParamCount==7);
 COpCodeSync::EndLocalCommands(); assert(argParamCount==7);
 puts("PASS T66 production capture guard: nested native calls restore enclosing argument context");
}
""")
