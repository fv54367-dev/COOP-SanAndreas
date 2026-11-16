// T67: the guest's own seat of the mission's machines, and what his machine does counts in the owner's script.
// While the mission authority's turret or RC toy is out ("Air Raid", "Supply Lines..."), each guest gets one of his
// own beside it (a minigun on the roof, an RC Baron by the owner's), and his gun's hits on the shared mission
// vehicles go to the authority as the copy's health: he applies the difference with the game's own
// CVehicle::InflictDamage, so the script counts the kill natively, once.
// Adapted from Karimov-Akbar/sa-coop (MIT), features/story/rc-toys/RcToys.cpp; see NOTICE.md.
#include "stdafx.h"
#include "CMissionMachines.h"
#include "CMissionObjectMoves.h"
#include "CMissionWarTable.h"
#include "CObjectSync.h"
#include "CConfigLoader.h"
#include "CCoopMission.h"
#include "COpCodeSync.h"
#include "CMissionMirror.h"
#include "SceneCompat/Game.h"
#include "SceneCompat/Script.h"
#include "network/packets/mission_machines.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
using P = Packets::Scripts::MachinePhase;
using D = Packets::Scripts::GuestMachineDamage;
using scene_game::script::Cmd;
namespace game = scene_game;

constexpr int kBaron = 464, kGoblin = 501;
constexpr int kMinigunModel = 362;  // the model of the weapon the turret carries
constexpr int kGun = 38;            // WEAPON_MINIGUN: the machines' guns (their hits must count as minigun ones)
constexpr int kTurretBase = 1485;   // #MINIGUN_BASE: what CJ is fixed to, out of sight (ZERO1's own is the mission's)
constexpr int kForklift = 530;      // the forklift of the depot (RYDER2 "Robbing Uncle Sam", T70)
constexpr int kCrate = 1558;        // #CJ_CARDBRD_PICKUP: the crates the forklift carries to the Mule (62@..86@; 1224 was wrong)
constexpr int kCopBike = 523, kPacker = 443;  // HEIST4 "Cop Wheels" (D): the bikes and the truck they are loaded on
// Zero's roof, where ZERO2 gives its player the RC Baron (ZERO2.txt 107-109, heading 40.09 at its line 196)
const CVector kRoof{-2205.44f, 128.99f, 57.33f};
constexpr float kRoofHeading = 40.09f;
// the war table of ZERO4 "New Model Army" (T75): where it gives its player the RC Goblin (ZERO4.txt 736-739, heading
// 90), an interior (area 10) the guest goes into for his own (CMissionWarTable)
const CVector kTable{-973.759f, 1077.152f, 1350.399f};
constexpr float kTableHeading = 90.0f;
constexpr uint8_t kTableArea = 10;
// the street at the shop's door: between where ZERO2 and ZERO4 put their player back, in neither
const CVector kDoor{-2244.55f, 134.5f, 34.32f};
constexpr float kNearShop = 300.0f;  // a guest farther from Zero's shop is brought to its door

P sent;                // authority: what the guests were last told
uint32_t sentAt = 0;   // authority: last send (change + a heartbeat every 2 s)
P phase;               // guest: the authority's machine
uint32_t phaseAt = 0;  // guest: when the last phase arrived (5 s without one turns it off)
struct Own
{
    bool out = false, turret = false, camera = false, declined = false, cameOver = false;
    bool pending = false;
    int pedRef = -1;
    bool bullet = false, fire = false, collision = false, melee = false, explosion = false;
    int base = -1;                       // our turret's invisible base object
    int heavy = 0, heavyAmmo = 0;        // what the minigun's slot held before
    int weapon = 0;                      // the gun CJ held
    CVector stood{};
    uint32_t waitUntil = 0, goneMs = 0, cameraMs = 0, floorUntil = 0;
    int forklift = -1;                   // our forklift of RYDER2 (a plain one of our own)
    uint32_t forkliftGoneMs = 0;
} own;
std::map<uint32_t, std::pair<CVector, uint32_t>> crateMoved;  // objectid -> last pos and when it moved
// guest: per shared mission vehicle, the last health told and when (150 ms at most per vehicle)
std::map<uint64_t, std::pair<float, uint32_t>> told;

void Message(const char* s) { CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} %s", s); }
bool On() { return CConfigLoader::ms_bCoopMissionMachines; }
bool Connected() { return CNetwork::m_bConnected && CNetwork::m_bAuthenticated; }
bool InCoop()
{
    return CCoopMission::IsRunning() && !CCoopMission::IsLocalIndividualMission() && !CCoopMission::HasLocalLeft();
}
CPlayerInfo* PlayerInfo() { return &CWorld::Players[0]; }
CVehicle* Toy() { return PlayerInfo()->m_pRemoteVehicle; }

// the running mission script the authority syncs (the same search CMissionGuestCounts does)
CRunningScript* MissionScript()
{
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (s->m_bIsMission && COpCodeSync::IsScriptSynced(s)) return s;
    return nullptr;
}

// this guest's rank among the others of the mission (0 = the first): the machine spots spread by rank, so with
// 3-4 players every guest gets one of its own and nobody stacks on anybody
int GuestRank(playerid_t owner)
{
    std::vector<playerid_t> ids;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        if (!player || player->m_playerId == owner || player->m_bIndividualMode || player->m_bDead) continue;
        ids.push_back(player->m_playerId);
    }
    std::sort(ids.begin(), ids.end(), [](playerid_t a, playerid_t b) { return a.value() < b.value(); });
    const auto me = CNetworkPlayerManager::m_localPlayerId;
    for (size_t i = 0; i < ids.size(); ++i)
        if (ids[i] == me) return int(i);
    return 0;
}

// --- authority side: the phase of my machine -----------------------------------------

P AuthorityPhase()
{
    P p{};
    p.owner = CNetworkPlayerManager::m_localPlayerId;
    auto script = MissionScript();
    auto me = FindPlayerPed(0);
    if (!script || !me) return p;
    if (!_strnicmp(script->m_szName, "ZERO1", 8) || !_strnicmp(script->m_szName, "HEIST5", 8))
    {
        // the turret: the invisible object CJ is fixed to (CPed::m_pAttachedTo, AttachPedToEntity 0x5E7CB0); the
        // same in HEIST5 "Up, Up and Away!" at the fuel depot (HEIST5.txt 246/863, #MINIGUN_BASE, T75)
        auto on = game::Field<CEntity*>(me, 0xFC);
        if (on && on->m_nType == ENTITY_TYPE_OBJECT && on->m_matrix)
        {
            p.kind = P::TURRET;
            p.pos = on->GetPosition();
            p.heading = std::atan2(-on->m_matrix->up.x, on->m_matrix->up.y) * 57.29578f;
        }
    }
    else if (!_strnicmp(script->m_szName, "ZERO2", 8))
    {
        auto toy = Toy();
        if (toy && toy->IsVTableValid() && toy->m_nModelIndex == kBaron)
        {
            p.kind = P::RC_BARON;
            p.pos = kRoof;
            p.heading = kRoofHeading;
        }
    }
    else if (!_strnicmp(script->m_szName, "RYDER2", 8))
    {
        // the depot's forklift: each guest gets one of his own and the crates move by the move channel
        for (auto networkVehicle : CNetworkVehicleManager::m_pVehicles)
        {
            auto* vehicle = networkVehicle && networkVehicle->m_bSyncing ? networkVehicle->m_pVehicle : nullptr;
            if (vehicle && vehicle->IsVTableValid() && vehicle->m_nCreatedBy == MISSION_VEHICLE &&
                vehicle->m_nModelIndex == kForklift && vehicle->m_matrix)
            {
                p.kind = P::FORKLIFT;
                p.pos = vehicle->GetPosition();
                p.heading = std::atan2(-vehicle->m_matrix->up.x, vehicle->m_matrix->up.y) * 57.29578f;
                break;
            }
        }
    }
    else if (!_strnicmp(script->m_szName, "ZERO4", 8))
    {
        // "New Model Army" (T75): the RC Goblin over the war table (RC.TakeCar of its 70@); the guest's own
        // magnet moves the barrels and planks through the object-move channel (CMissionWarTable)
        auto toy = Toy();
        if (toy && toy->IsVTableValid() && toy->m_nModelIndex == kGoblin)
        {
            p.kind = P::RC_GOBLIN;
            p.pos = kTable;
            p.heading = kTableHeading;
            p.area = kTableArea;
        }
    }
    return p;
}

// --- guest side: back out of a machine ----------------------------------------------

void Back(CPed* me)
{
    own.pending = true;
    if (!me || !CPools::ms_pPedPool || !CPools::ms_pPedPool->IsObjectValid(me) || !me->IsVTableValid() ||
        me->m_fHealth <= 0 || me->m_nPedFlags.bInVehicle || CCutsceneMgr::ms_running ||
        CCutsceneMgr::ms_cutsceneLoadStatus || TheCamera.m_bWideScreenOn) return;
    if (own.pedRef >= 0 && CPools::GetPed(own.pedRef) != me)
    {
        if (own.base >= 0) Cmd(0x0108).I(own.base).Run();
        own = Own{};
        return;
    }
    own.pending = false;
    int h = CPools::GetPedRef(me);
    if (own.turret)
    {
        Cmd(0x0465).I(h).Run();  // DETACH_CHAR_FROM_CAR, from an object too: the engine puts the held gun back
        if (own.base >= 0) Cmd(0x0108).I(own.base).Run();  // DELETE_OBJECT
        Cmd(0x0555).I(h).I(kGun).Run();  // REMOVE_WEAPON_FROM_CHAR: the minigun was the turret's
        if (own.heavy > 0)
        {
            int model = -1;
            Cmd(0x0781).I(own.heavy).Out(&model).Run();  // GET_WEAPONTYPE_MODEL
            if (model > 0 && CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
            {
                CStreaming::RequestModel(model, eStreamingFlags::MISSION_REQUIRED);
                CStreaming::LoadAllRequestedModels(false);
            }
            if (model > 0 && CStreaming::ms_aInfoForModel[model].m_nLoadState == LOADSTATE_LOADED)
                Cmd(0x01B2).I(h).I(own.heavy).I(own.heavyAmmo).Run();  // GIVE_WEAPON_TO_CHAR
        }
        Cmd(0x02AB).I(h).I(own.bullet).I(own.fire).I(own.explosion).I(own.collision).I(own.melee).Run();  // restore saved proofs
        Cmd(0x00A1).I(h).F(own.stood.x).F(own.stood.y).F(own.stood.z - 1.0f).Run();  // SET_CHAR_COORDINATES (feet)
        own.base = -1;
        own.turret = false;
    }
    else
    {
        Cmd(0x04D7).I(h).I(0).Run();  // FREEZE_CHAR_POSITION off
    }
    if (own.camera) Cmd(0x02EB).Run();  // RESTORE_CAMERA_JUMPCUT
    if (own.weapon) Cmd(0x01B9).I(h).I(own.weapon).Run();  // SET_CURRENT_CHAR_WEAPON
    own.out = own.camera = false;
    own.waitUntil = 0;
    own.weapon = 0;
    own.goneMs = GetTickCount();
}

void DropToy()
{
    // REMOVE_RC_BUGGY only takes it from the player; a plane left in the air fell and blew up (sa-coop): it goes too
    auto t = Toy();
    int h = t ? CPools::GetVehicleRef(t) : -1;
    Cmd(0x04DB).Run();  // REMOVE_RC_BUGGY
    if (h >= 0 && CPools::GetVehicle(h)) Cmd(0x00A6).I(h).Run();  // DELETE_CAR
}

void TakeAway(CPed* me)
{
    own.declined = own.cameOver = false;  // the authority's next part offers one again
    if (!own.out && !own.waitUntil) return;
    if (!me || me->m_fHealth <= 0 || me->m_nPedFlags.bInVehicle || CCutsceneMgr::ms_running ||
        CCutsceneMgr::ms_cutsceneLoadStatus || TheCamera.m_bWideScreenOn) { own.pending = true; return; }
    if (Toy()) DropToy();
    Back(me);
}

bool FloorWait(CPed* me)
{
    if (!own.floorUntil) return false;
    bool found = false;
    float z = scene_game::Call<0x5696C0, float>(own.stood.x, own.stood.y, own.stood.z + 1.0f, &found,
                                               (CEntity**)nullptr);  // CWorld::FindGroundZFor3DCoord
    if ((!found || z < own.stood.z - 2.5f) && GetTickCount() < own.floorUntil) return true;
    Cmd(0x04D7).I(CPools::GetPedRef(me)).I(0).Run();  // FREEZE_CHAR_POSITION off
    own.floorUntil = 0;
    return false;
}

// --- guest side: take a machine of our own ------------------------------------------

// beside the authority's turret: the roof's floor as high as the turret's, room for CJ (spread by rank)
bool TurretSpot(CVector& out, int rank)
{
    for (float r : {2.5f, 3.5f, 1.8f})
        for (int k = 0; k < 12; ++k)
        {
            float a = float(k + rank * 4) * 0.5235988f;
            CVector p = phase.pos + CVector{std::cos(a) * r, std::sin(a) * r, 0.0f};
            bool found = false;
            float z = scene_game::Call<0x5696C0, float>(p.x, p.y, phase.pos.z + 1.5f, &found,
                                                       (CEntity**)nullptr);  // CWorld::FindGroundZFor3DCoord
            if (!found || std::fabs(z - phase.pos.z) > 0.6f) continue;
            CVector body{p.x, p.y, z + 1.0f};
            if (scene_game::Call<0x569E20, CEntity*>(body, 0.4f, (CEntity*)nullptr, true, true, false, true, false,
                                                     false))  // CWorld::TestSphereAgainstWorld
                continue;
            out = {p.x, p.y, z};
            return true;
        }
    return false;
}

// "Air Raid": a minigun of our own as the mission makes the authority's, an object out of sight with no collision
// and CJ fixed to it (ZERO1.txt 739: the same offset, the whole round)
void GiveTurret(CPed* me, int rank)
{
    CVector at;
    if (!TurretSpot(at, rank))
    {
        own.goneMs = GetTickCount();  // again in a while
        return;
    }
    int h = CPools::GetPedRef(me);
    Cmd(0x0107).I(kTurretBase).F(at.x).F(at.y).F(at.z).Out(&own.base).Run();  // CREATE_OBJECT
    if (own.base < 0) return;
    own.pedRef = CPools::GetPedRef(me);
    own.stood = me->GetPosition();
    own.weapon = me->GetWeapon().m_eWeaponType;
    auto info = CWeaponInfo::GetWeaponInfo((eWeaponType)kGun, (unsigned char)1);
    const int slot = info ? (int)info->m_nSlot : 7;
    own.heavy = me->m_aWeapons[std::clamp(slot, 0, 12)].m_eWeaponType;
    own.heavyAmmo = (int)me->m_aWeapons[std::clamp(slot, 0, 12)].m_nTotalAmmo;
    Cmd(0x0382).I(own.base).I(0).Run();                // SET_OBJECT_COLLISION
    Cmd(0x0750).I(own.base).I(0).Run();                // SET_OBJECT_VISIBLE
    Cmd(0x0177).I(own.base).F(phase.heading).Run();    // SET_OBJECT_HEADING
    // as ZERO1 fixes its player: this offset, the whole round, the minigun (the attachment gives it)
    Cmd(0x04F4).I(h).I(own.base).F(-0.25f).F(-0.5f).F(1.25f).I(2).F(360.0f).I(kGun).Run();  // ATTACH_CHAR_TO_OBJECT
    Cmd(0x01B9).I(h).I(kGun).Run();                    // SET_CURRENT_CHAR_WEAPON
    Cmd(0x0858).I(0).F(270.0f).F(300.0f).Run();        // SET_HEADING_FOR_ATTACHED_PLAYER, as ZERO1
    own.bullet = me->m_nPhysicalFlags.bBulletProof; own.fire = me->m_nPhysicalFlags.bFireProof; own.explosion = me->m_nPhysicalFlags.bExplosionProof;
    own.collision = me->m_nPhysicalFlags.bCollisionProof; own.melee = me->m_nPhysicalFlags.bMeleeProof;
    Cmd(0x02AB).I(h).I(1).I(1).I(1).I(1).I(1).Run();   // SET_CHAR_PROOFS: Berkley's planes bomb the roof
    own.out = own.turret = true;
    Message("Tu propio bunker de minigun junto al de la mision. La tecla de subir borra tu puesto.");
}

// the floor under the toy's place, with room around (CRemote puts the toy on the ground under the point given;
// the check keeps clear of the authority's toy by rank)
bool ToyPlace(CVector& out, int rank)
{
    const float below = phase.area ? 7.0f : 1.5f;  // the war table lies 5 m under the owner's toy (sa-coop, T75)
    for (float r : {6.0f, 4.0f, 8.0f})
        for (int k = 0; k < 12; ++k)
        {
            float a = phase.heading * 0.0174533f + float(k + rank * 4) * 0.5235988f;
            CVector p = phase.pos + CVector{std::cos(a) * r, std::sin(a) * r, 0.0f};
            bool found = false;
            float z = scene_game::Call<0x5696C0, float>(p.x, p.y, phase.pos.z + 1.0f, &found,
                                                       (CEntity**)nullptr);  // CWorld::FindGroundZFor3DCoord
            if (!found || z < phase.pos.z - below || z > phase.pos.z + 1.0f) continue;
            CVector body{p.x, p.y, z + 1.2f};
            if (scene_game::Call<0x569E20, CEntity*>(body, 1.0f, (CEntity*)nullptr, true, true, false, true, false,
                                                     false))  // CWorld::TestSphereAgainstWorld
                continue;
            out = {p.x, p.y, z + 0.5f};
            return true;
        }
    return false;
}

void Launch(const CVector& at)
{
    Cmd(0x046E).I(0).F(at.x).F(at.y).F(at.z).F(phase.heading).I(phase.kind == P::RC_GOBLIN ? kGoblin : kBaron)
        .Run();  // GIVE_REMOTE_CONTROLLED_MODEL_TO_PLAYER
    // made in area 0: at the war table a toy nobody saw, its guest included (sa-coop); ZERO4 sets its own there (T75)
    if (phase.area && Toy()) Cmd(0x0840).I(CPools::GetVehicleRef(Toy())).I(phase.area).Run();  // SET_VEHICLE_AREA_VISIBLE
    Cmd(0x048A).I(0).Run();              // SET_ENABLE_RC_DETONATE off, as ZERO2 (on, the fire button blew it up)
    Cmd(0x04D6).I(0).Run();              // SET_ENABLE_RC_DETONATE_ON_CONTACT off
    own.out = true;
    own.camera = false;  // CRemote puts the camera on the toy
    Message(phase.kind == P::RC_GOBLIN
                ? "Tu propio helicoptero a radiocontrol con iman. Disparar levanta y suelta barriles y tablas. La tecla de subir te devuelve a CJ."
                : "Tu propio avion a radiocontrol. Ayudales a derribar las metas. La tecla de subir te devuelve a CJ.");
}

// waiting for the floor under the toy's place: at once when it is there, 8 s at most
void ToyWait(CPed* me, int rank)
{
    CVector at;
    if (ToyPlace(at, rank))
    {
        own.waitUntil = 0;
        return Launch(at);
    }
    if (GetTickCount() < own.waitUntil) return;
    Back(me);
    own.declined = true;  // not the void again and again
}

// a guest far from Zero's shop is brought to its door (the authority's mission moved him there; ZERO2 parks its
// player in a frozen vehicle meanwhile): not out of an activity of its own, a shop or death
void ComeOver(CPed* me)
{
    own.cameOver = true;
    if (*reinterpret_cast<uint8_t*>(0xA444A8) || me->m_fHealth <= 0.0f) return;  // CTheScripts::bMiniGameInProgress
    int h = CPools::GetPedRef(me);
    auto v = me->m_nPedFlags.bInVehicle && me->m_pVehicle ? me->m_pVehicle : nullptr;
    if (v && (me->m_nAreaCode != 0 || v->m_pDriver != me))
        Cmd(0x0362).I(h).F(kDoor.x).F(kDoor.y).F(kDoor.z).Run();  // WARP_CHAR_FROM_CAR_TO_COORD
    Cmd(0x04BB).I(0).Run();                              // SET_AREA_VISIBLE
    Cmd(0x0860).I(h).I(0).Run();                         // SET_CHAR_AREA_VISIBLE
    Cmd(0x03CB).F(kDoor.x).F(kDoor.y).F(kDoor.z).Run();  // LOAD_SCENE: the roof's floor with it
    Cmd(0x00A1).I(h).F(kDoor.x).F(kDoor.y).F(kDoor.z - 1.0f).Run();  // SET_CHAR_COORDINATES (feet)
}

void GiveToy(CPed* me, int rank)
{
    int h = CPools::GetPedRef(me);
    own.pedRef = CPools::GetPedRef(me);
    own.stood = me->GetPosition();
    own.weapon = me->GetWeapon().m_eWeaponType;
    Cmd(0x01B9).I(h).I(0).Run();  // SET_CURRENT_CHAR_WEAPON: bare hands, the fire button fires the toy's gun
    Cmd(0x04D7).I(h).I(1).Run();  // FREEZE_CHAR_POSITION: the sticks drive the toy, not CJ
    if (phase.area) CMissionWarTable::Enter(me, phase.pos, phase.area);  // the war table: CJ into its area first (T75)
    own.waitUntil = GetTickCount() + 8000;
    ToyWait(me, rank);
}

void PartnerUpdate(CPed* me, int rank)
{
    if (own.waitUntil) return ToyWait(me, rank);
    if (own.out && (own.turret ? game::Field<CEntity*>(me, 0xFC) == nullptr : Toy() == nullptr))
    {
        // shot down, or something took CJ off the turret
        Back(me);
        return;
    }
    const uint32_t now = GetTickCount();
    if (now - phaseAt > 5000) return TakeAway(me);  // the heartbeat stopped: the machine part is over
    const CPad* pad = CPad::GetPad(0);
    if (own.out && pad->NewState.ButtonTriangle && !pad->OldState.ButtonTriangle)
    {
        // the enter key: CJ back for the rest of the authority's part
        if (Toy()) DropToy();
        Back(me);
        own.declined = true;
        return;
    }
    // the camera back on our toy (CRemote gives it with CCamera::TakeControl): a scene of the mission takes it and
    // gives it to CJ, and the sticks flew a toy out of sight (sa-coop, 31.10)
    if (own.out && !own.turret && Toy() && !CCutsceneMgr::ms_running && !CCutsceneMgr::ms_cutsceneLoadStatus &&
        now - own.cameraMs > 1000)
    {
        own.cameraMs = now;
        scene_game::CallMethod<0x50C7C0>(&TheCamera, (CEntity*)Toy(), 0x12, 1, 1);  // CCamera::TakeControl, BEHINDCAR
    }
    // no toy while a scene holds the camera: one given then was never in the picture
    if (own.out || own.declined || Toy() || CCutsceneMgr::ms_running || CCutsceneMgr::ms_cutsceneLoadStatus ||
        now - own.goneMs < 8000)
        return;
    const bool turret = phase.kind == P::TURRET;
    // the toys are Zero's (a guest far from the shop is brought to its door); a turret is wherever the owner's is
    if ((me->GetPosition() - (turret ? phase.pos : kRoof)).Magnitude2D() > kNearShop)
    {
        if (!turret && !own.cameOver) ComeOver(me);
        return;
    }
    if (me->m_nPedFlags.bInVehicle || me->m_nAreaCode != 0 || me->m_fHealth <= 0.0f) return;
    const int model = turret ? kTurretBase : (phase.kind == P::RC_GOBLIN ? kGoblin : kBaron);
    bool ready = CStreaming::ms_aInfoForModel[model].m_nLoadState == LOADSTATE_LOADED;
    if (turret && CStreaming::ms_aInfoForModel[kMinigunModel].m_nLoadState != LOADSTATE_LOADED) ready = false;
    if (!ready)
    {
        CStreaming::RequestModel(model, eStreamingFlags::MISSION_REQUIRED);
        if (turret) CStreaming::RequestModel(kMinigunModel, eStreamingFlags::MISSION_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
        return;
    }
    if (turret) GiveTurret(me, rank);
    else GiveToy(me, rank);
}

// what our machine did to the authority's shared mission vehicles: the copy's health after our hits, 150 ms at
// most per vehicle, only ever down (the authority applies the difference with InflictDamage)
void ReportDamage()
{
    const uint32_t now = GetTickCount();
    for (auto networkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (!networkVehicle || networkVehicle->m_bSyncing) continue;  // our own vehicles are the others' worry
        auto vehicle = networkVehicle->m_pVehicle;
        if (!vehicle || !CPools::ms_pVehiclePool || !CPools::ms_pVehiclePool->IsObjectValid(vehicle) || !vehicle->IsVTableValid() || vehicle->m_nCreatedBy != MISSION_VEHICLE) continue;
        const float health = std::max(vehicle->m_fHealth, 0.0f);
        const uint64_t id = networkVehicle->m_vehicleId.packed();
        auto it = told.find(id);
        if (it != told.end())
        {
            if (health >= it->second.first || now - it->second.second < 150) continue;
            it->second = {health, now};
        }
        else
        {
            told.emplace(id, std::make_pair(health, now));
            if (health <= 0.0f) continue;  // nothing to tell about a vehicle that arrived dead
        }
        D d{};
        d.vehicleid = networkVehicle->m_vehicleId;
        d.health = health;
        d.weapon = (uint8_t)kGun;
        d.pos = vehicle->GetPosition();
        GetPacketFactory().Send(d);
    }
}
// --- RYDER2 "Robbing Uncle Sam": our own forklift and the crates of the depot (T70) ------------------

// a free spot on the yard beside the authority's forklift, spread by rank
bool ForkliftSpot(CVector& out, int rank)
{
    for (float r : {4.0f, 5.5f, 3.0f, 7.0f, 9.0f})
        for (int k = 0; k < 12; ++k)
        {
            float a = phase.heading * 0.0174533f + float(k + rank * 4) * 0.5235988f;
            CVector p = phase.pos + CVector{std::cos(a) * r, std::sin(a) * r, 0.0f};
            bool found = false;
            float z = scene_game::Call<0x5696C0, float>(p.x, p.y, phase.pos.z + 1.5f, &found, (CEntity**)nullptr);
            if (!found || std::fabs(z - phase.pos.z) > 2.0f) continue;
            CVector body{p.x, p.y, z + 1.2f};
            if (scene_game::Call<0x569E20, CEntity*>(body, 1.2f, (CEntity*)nullptr, true, true, false, true, false,
                                                     false))  // CWorld::TestSphereAgainstWorld
                continue;
            out = {p.x, p.y, z};
            return true;
        }
    return false;
}

CVehicle* OurForklift()
{
    auto* lift = own.forklift >= 0 ? CPools::GetVehicle(own.forklift) : nullptr;
    if (lift && !lift->IsVTableValid()) lift = nullptr;
    if (lift && lift->m_fHealth <= 0.0f) lift = nullptr;
    return lift;
}

// claims go away when the machine stops carrying; the last position told stands, and the owner's own check
// (Car.IsTouchingObject of the carts) counts the crate when our copy stands at his
void ReportCrates(CPed* me, CVehicle* lift)
{
    const uint32_t now = GetTickCount();
    const bool driving = me->m_nPedFlags.bInVehicle && me->m_pVehicle == lift;
    for (auto& [id, copy] : CObjectSync::GetPhotoTargets(false))
    {
        if (!copy || copy->m_nModelIndex != kCrate || !copy->m_matrix) continue;
        const CVector& at = copy->m_matrix->pos;
        auto moved = crateMoved.find(id);
        const bool itMoved = moved != crateMoved.end() && (at - moved->second.first).Magnitude() > 0.03f;
        if (itMoved)
            moved->second = {at, now};
        else if (moved == crateMoved.end())
            crateMoved.emplace(id, std::make_pair(at, now));
        const bool claimed = CMissionObjectMoves::IsClaimed(id);
        const float d = (at - lift->GetPosition()).Magnitude();
        if (claimed)
        {
            if (d > 7.0f || (!driving && (!itMoved || now - moved->second.second > 1500)))
                CMissionObjectMoves::Unclaim(id);
            else if (itMoved)
                CMissionObjectMoves::Report(id);
        }
        // claimed when our forks get to it, not once it moved: the unclaimed copy is static with infinite mass
        // (CObjectSync), so it never moved and the forklift hit it like a wall (Robbing Uncle Sam, 2026-11-13)
        else if (driving && d < 4.0f)
        {
            if (CMissionObjectMoves::Claim(id))
                CMissionMirror::LogEventf("forklift-crate-claim id=%u d=%.1f", id, d);
        }
    }
}

void GuestForkliftEnd(CPed* me)
{
    for (const auto& [id, ignored] : crateMoved) CMissionObjectMoves::Unclaim(id);
    crateMoved.clear();
    if (auto* lift = OurForklift())
    {
        if (me && me->m_nPedFlags.bInVehicle && me->m_pVehicle == lift)
            Cmd(0x0362).I(CPools::GetPedRef(me)).F(lift->GetPosition().x).F(lift->GetPosition().y)
                .F(lift->GetPosition().z + 2.0f).Run();  // WARP_CHAR_FROM_CAR_TO_COORD: not under a deleted car
        Cmd(0x00A6).I(own.forklift).Run();  // DELETE_CAR: our forklift goes with the part
    }
    if (own.forklift >= 0) own.forkliftGoneMs = GetTickCount();
    own.forklift = -1;
}

// our own forklift beside the authority's, and the depot crates we move with it
void GuestForklift(CPed* me, int rank)
{
    const uint32_t now = GetTickCount();
    auto* lift = OurForklift();
    if (!lift)
    {
        for (const auto& [id, ignored] : crateMoved) CMissionObjectMoves::Unclaim(id);
        crateMoved.clear();
        if (own.forklift >= 0)
        {
            own.forklift = -1;
            own.forkliftGoneMs = now;  // it just died or the part ended: a pause before the next one
        }
        if (now - phaseAt > 5000 || now - own.forkliftGoneMs < 8000) return;
        // a guest still in the Greenwood gets his forklift too (it stands beside; he walks to it): one per player
        if (me->m_nAreaCode != 0 || me->m_fHealth <= 0.0f || CCutsceneMgr::ms_running || CCutsceneMgr::ms_cutsceneLoadStatus)
            return;
        if (CStreaming::ms_aInfoForModel[kForklift].m_nLoadState != LOADSTATE_LOADED)
        {
            CStreaming::RequestModel(kForklift, eStreamingFlags::MISSION_REQUIRED);
            CStreaming::LoadAllRequestedModels(false);
            return;
        }
        CVector at;
        if (!ForkliftSpot(at, rank))
        {
            static uint32_t lastLog = 0;
            if (now - lastLog > 5000)
            {
                lastLog = now;
                CMissionMirror::LogEventf("forklift-guest-nospot at=%.1f,%.1f,%.1f", phase.pos.x, phase.pos.y, phase.pos.z);
            }
            return;
        }
        int h = -1;
        Cmd(0x00A5).I(kForklift).F(at.x).F(at.y).F(at.z).Out(&h).Run();  // CREATE_VEHICLE
        if (h < 0)
        {
            own.forkliftGoneMs = now;
            return;
        }
        own.forklift = h;
        CMissionMirror::LogEventf("forklift-guest-create at=%.1f,%.1f,%.1f rank=%d", at.x, at.y, at.z, rank);
        Cmd(0x0175).I(h).F(phase.heading).Run();  // SET_CAR_HEADING, facing like the authority's
        return;
    }
    if (now - phaseAt > 5000) { GuestForkliftEnd(me); return; }  // the heartbeat stopped: the part is over
    if (me->m_fHealth <= 0.0f) { GuestForkliftEnd(me); return; }  // dead: CJ alone, nothing left behind
    ReportCrates(me, lift);
}

// D (Cop Wheels, HEIST4): a guest rides one of the owner's mission bikes up to the Packer; the owner's script counts
// it natively (CMissionGuestCounts answers its IS_CHAR_IN_CAR / angled-area checks with the guest) and attaches the
// bike to the truck (ATTACH_CAR_TO_CAR is replayed here for HEIST4 only). Once our bike is on the truck we step off
// beside it: CJ is not carried away and the bike stops being driven from here.
void GuestBikeLoaded(CPed* me)
{
    if (!me->m_nPedFlags.bInVehicle || !me->m_pVehicle) return;
    CVehicle* bike = me->m_pVehicle;
    if (!bike->IsVTableValid() || bike->m_nModelIndex != kCopBike || bike->m_nCreatedBy != MISSION_VEHICLE) return;
    auto* truck = reinterpret_cast<CVehicle*>(bike->m_pAttachedTo);
    if (!truck || !CPools::ms_pVehiclePool || !CPools::ms_pVehiclePool->IsObjectValid(truck) ||
        !truck->IsVTableValid() || truck->m_nModelIndex != kPacker || !truck->m_matrix)
        return;
    CVector at = truck->GetPosition();
    at.x += truck->m_matrix->right.x * 4.0f;
    at.y += truck->m_matrix->right.y * 4.0f;
    bool found = false;
    const float z = scene_game::Call<0x5696C0, float>(at.x, at.y, at.z + 2.0f, &found, (CEntity**)nullptr);
    if (found && std::fabs(z - at.z) < 3.0f) at.z = z + 1.0f;
    Cmd(0x0362).I(CPools::GetPedRef(me)).F(at.x).F(at.y).F(at.z).Run();  // WARP_CHAR_FROM_CAR_TO_COORD
    Message("La moto ya esta en el camion: te bajaste al lado.");
}

}  // namespace

void CMissionMachines::Init() {}

void CMissionMachines::Reset()
{
    sent = phase = P{};
    sentAt = phaseAt = 0;
    if (auto me = FindPlayerPed(0))
    {
        TakeAway(me);
        GuestForkliftEnd(me);
    }
    CMissionObjectMoves::Reset();
    told.clear();
}

void CMissionMachines::Process()
{
    auto me = FindPlayerPed(0);
    if (own.pending) { TakeAway(me); if (own.pending) return; }
    if (!On() || !Connected() || !me || !me->IsVTableValid())
    {
        if (me) TakeAway(me);
        CMissionObjectMoves::Reset();
        own.forklift = -1;  // our machines go with the world; nothing to delete on a dead session
        sent = phase = P{};
        told.clear();
        return;
    }
    const uint32_t now = GetTickCount();
    if (CCoopMission::IsScriptAuthority())
    {
        TakeAway(me);  // the authority never runs a guest machine of his own
        P p = AuthorityPhase();
        if (p.kind != sent.kind || now - sentAt >= 2000)  // change + heartbeat (a lost OFF expires on the guest)
        {
            sent = p;
            sentAt = now;
            GetPacketFactory().Send(p);
        }
        return;
    }
    if (!InCoop())
    {
        TakeAway(me);
        GuestForkliftEnd(me);
        phase = P{};
        told.clear();
        return;
    }
    GuestBikeLoaded(me);
    if (phase.kind == P::FORKLIFT)
        GuestForklift(me, GuestRank(phase.owner));
    else
        PartnerUpdate(me, GuestRank(phase.owner));
    ReportDamage();
}

bool CMissionMachines::GuestToyPending() { return own.out || own.waitUntil != 0; }

void CMissionMachines::OnPhase(const P& p)
{
    if (!On() || !Connected() || CCoopMission::IsScriptAuthority() || p.kind > P::FORKLIFT) return;
    if (p.kind != phase.kind)
    {
        if (auto me = FindPlayerPed(0))
        {
            if (own.out) TakeAway(me);
            GuestForkliftEnd(me);
        }
        else if (own.out) own.pending = true;
    }
    phase = p;
    phaseAt = GetTickCount();
}

void CMissionMachines::OnDamage(const D& p)
{
    if (!On() || !Connected() || !CCoopMission::IsScriptAuthority() || !CCoopMission::IsRunning()) return;
    auto networkVehicle = CNetworkVehicleManager::GetVehicle(p.vehicleid);
    auto vehicle = networkVehicle ? networkVehicle->m_pVehicle : nullptr;
    if (!vehicle || !CPools::ms_pVehiclePool || !CPools::ms_pVehiclePool->IsObjectValid(vehicle) || !vehicle->IsVTableValid() || !networkVehicle->m_bSyncing) return;  // only my own hosted ones
    if (vehicle->m_nCreatedBy != MISSION_VEHICLE || vehicle->m_fHealth <= 0.0f) return;
    const float delta = vehicle->m_fHealth - p.health;
    if (delta <= 0.0f) return;
    auto player = CNetworkPlayerManager::GetPlayer(p.senderPlayerid);
    if (!player || !player->m_pPed || !CPools::ms_pPedPool ||
        !CPools::ms_pPedPool->IsObjectValid(player->m_pPed) || !player->m_pPed->IsVTableValid()) return;
    // InflictDamage first asks CanVehicleBeDamaged (0x6D1280, gta-reversed Vehicle.cpp): with
    // SET_CAR_ONLY_DAMAGED_BY_PLAYER (bInvulnerable) it refuses all but FindPlayerPed(). A coop player counts as the
    // player (BCRASH1's witness car); the flag is lifted only around this call.
    const bool lift = CConfigLoader::ms_bVehicleDamageAnyPlayer && vehicle->m_nPhysicalFlags.bInvulnerable;
    if (lift) vehicle->m_nPhysicalFlags.bInvulnerable = false;
    // the game's own damage path: records the weapon, explodes at 0 (gta-reversed Vehicle.cpp InflictDamage)
    reinterpret_cast<void(__thiscall*)(CVehicle*, CEntity*, int, float, float, float, float)>(0x6D7C90)(
        vehicle, player->m_pPed, kGun, delta, p.pos.x, p.pos.y, p.pos.z);
    if (lift && CPools::ms_pVehiclePool->IsObjectValid(vehicle)) vehicle->m_nPhysicalFlags.bInvulnerable = true;
    CMissionMirror::LogEventf("machine-damage vehicle=%llu player=%u delta=%.0f", (unsigned long long)p.vehicleid.packed(),
                              p.senderPlayerid.value.value(), delta);
}
