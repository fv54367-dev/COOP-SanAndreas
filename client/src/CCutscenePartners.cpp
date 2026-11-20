#include "stdafx.h"
#include "CCutscenePartners.h"
#include "SceneCompat/ScenePartner.h"
#include "SceneCompat/SceneMocap.h"
#include "CCoopMission.h"
#include "CMissionMode.h"
#include "CMissionMirror.h"
#include "CCameraMirror.h"
#include "Commands/Commands/CCommandTeleportPlayersToHostSafely.h"
#include "Commands/Commands/CCommandPutNetworkPlayersInCar.h"
#include <algorithm>
#include <map>
#include <set>
#include <CAutomobile.h>

namespace {
using Snapshot = Packets::Scripts::CutscenePartners;
Snapshot current;
uint32_t lastSend = 0, lastReceive = 0, lastReady = 0, lastSequence = 0;
uint32_t nextSession = 0;
bool prepared = false, running = false, haveSpot = false;
char preparedName[9]{};
std::map<uint32_t,uint32_t> ready;
struct Saved {
    int handle;
    CVector pos;
    unsigned char area;
    bool visible;
    bool dontAcceptLookAts;
};
std::map<playerid_t, Saved> saved;
playerid_t Owner();
struct EntryRide { int pedHandle = -1; int model = -1; unsigned char c1 = 0, c2 = 0; };
std::map<playerid_t, EntryRide> beforeMission, entryRides;
struct DecorRide { int handle; CVehicle* vehicle; };
std::map<playerid_t, DecorRide> decorRides;
int observedMission = -1;
bool scriptedHeld = false;

int MissionId() {
    if (CCoopMission::IsScriptAuthority()) return CMissionMirror::GetRunningMissionId();
    auto owner = CNetworkPlayerManager::GetPlayer(Owner());
    return owner ? owner->m_nRunningMission : -1;
}
void ObserveEntryRides() {
    int mission = MissionId();
    if (mission != observedMission) {
        entryRides = beforeMission;
        observedMission = mission;
        if (mission == 15) for (const auto& [id, ride] : entryRides)
            CMissionMirror::LogEventf("scene-entry-car player=%u model=%d ped=%d",id.value(),ride.model,ride.pedHandle);
    }
    auto remember = [](playerid_t id, CPed* ped) {
        EntryRide ride;
        if (ped) ride.pedHandle = CPools::GetPedRef(ped);
        if (ped && ped->m_nPedFlags.bInVehicle && ped->m_pVehicle &&
            CPools::ms_pVehiclePool->IsObjectValid(ped->m_pVehicle)) {
            auto* car = ped->m_pVehicle;
            auto* info = static_cast<CVehicleModelInfo*>(CModelInfo::ms_modelInfoPtrs[car->m_nModelIndex]);
            if (info && info->m_nVehicleType == VEHICLE_AUTOMOBILE) {
                ride.model = car->m_nModelIndex; ride.c1 = car->m_nPrimaryColor; ride.c2 = car->m_nSecondaryColor;
            }
        }
        beforeMission[id] = ride;
    };
    beforeMission.clear();
    remember(CNetworkPlayerManager::m_localPlayerId, FindPlayerPed(0));
    for (auto p : CNetworkPlayerManager::m_pPlayers) remember(p->m_playerId, p->m_pPed);
}
void DeleteDecorRides() {
    for (auto& [id, ride] : decorRides) if (CPools::GetVehicle(ride.handle) == ride.vehicle) {
        CWorld::Remove(ride.vehicle);
        delete ride.vehicle;
    }
    decorRides.clear();
}
// Seat dummies are the native GetFrontSeatPosn/GetBackSeatPosn (gta-reversed VehicleModelInfo.h).
CVector Seat(CVehicle* car, int seat) {
    auto* info = static_cast<CVehicleModelInfo*>(CModelInfo::ms_modelInfoPtrs[car->m_nModelIndex]);
    if (!info || !info->m_pVehicleStruct) return {};
    CVector pos = info->m_pVehicleStruct->m_avDummyPos[seat == 0 ? 4 : 5];
    pos.x = (seat == 1 ? -1.0f : 1.0f) * fabsf(pos.x);
    return pos;
}
bool DecorateRide(playerid_t slot, CPed* ped, int rank) {
    auto entry = entryRides.find(slot);
    if (entry == entryRides.end() || entry->second.model < 0) return false;
    auto source = CNetworkPlayerManager::GetPlayer(slot);
    auto* sourcePed = slot == CNetworkPlayerManager::m_localPlayerId ? FindPlayerPed(0) : source ? source->m_pPed : nullptr;
    if (!sourcePed || CPools::GetPed(entry->second.pedHandle) != sourcePed) return false;
    auto it = decorRides.find(slot);
    if (it == decorRides.end()) {
        int model = entry->second.model;
        if (CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED ||
            CPools::ms_pVehiclePool->GetNoOfFreeSpaces() < 12) return false;
        auto* car = new CAutomobile(model, MISSION_VEHICLE, true);
        if (!car) return false;
        car->m_nPrimaryColor = entry->second.c1; car->m_nSecondaryColor = entry->second.c2;
        car->m_bUsesCollision = false; car->m_bIsVisible = false;
        car->m_nPhysicalFlags.bDisableMoveForce = true;
        car->m_nPhysicalFlags.bDisableTurnForce = true;
        it = decorRides.emplace(slot, DecorRide{CPools::GetVehicleRef(car), car}).first;
        // Direct CWorld::Add bypasses the SCM/traffic call sites which publish network vehicles.
        CWorld::Add(car);
        CMissionMirror::LogEventf("scene-decor-car slot=%u model=%d handle=%d rank=%d",slot.value(),model,it->second.handle,rank);
    }
    auto* car = it->second.vehicle;
    if (CPools::GetVehicle(it->second.handle) != car) return false;
    auto* local = FindPlayerPed(0);
    if (!local) return false;
    car->m_nAreaCode = local->m_nAreaCode;
    auto* col = car->GetColModel();
    float length = col ? col->m_boundBox.m_vecMax.y - col->m_boundBox.m_vecMin.y : 5.0f;
    if (!coop::scene::CarBehind(reinterpret_cast<scene_game::CEntity*>(car), rank, length)) return false;
    auto seat = Seat(car, 0);
    seat.x = -fabsf(seat.x);  // driver of the decorative car
    car->m_vecMoveSpeed = {}; car->m_vecTurnSpeed = {};
    coop::scene::HoldVehicleSeat(reinterpret_cast<scene_game::CPed*>(ped),
        reinterpret_cast<scene_game::CEntity*>(car), {seat.x, seat.y, seat.z});
    return true;
}
playerid_t Owner() {
    if (CCoopMission::IsRunning()) return CCoopMission::GetOwnerId();
    if (CCoopMission::IsScriptAuthority()) return CNetworkPlayerManager::m_localPlayerId;
    for (auto p : CNetworkPlayerManager::m_pPlayers) if(p->m_bIsHost) return p->m_playerId;
    return {};
}
bool Eligible() {
    return CConfigLoader::ms_bCutscenePartners && CNetwork::m_bConnected && CNetwork::m_bAuthenticated &&
        !CMissionMode::IsIndividual() && !CCoopMission::IsLocalIndividualMission() && !CCoopMission::HasLocalLeft() &&
        (!CCoopMission::IsActive() || CCoopMission::IsLocalParticipant()) &&
        CTheScripts::OnAMissionFlag && CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] && Owner().valid();
}
bool SameScene(const Snapshot& p) {
    return CCutsceneMgr::ms_running && !_strnicmp(p.scene, CCutsceneMgr::ms_cutsceneName, 8);
}
coop::scene::Spot Spot(const Snapshot& p) {
    coop::scene::Spot s;
    s.ground = {p.ground.x,p.ground.y,p.ground.z}; s.heading = p.heading;
    s.pose = static_cast<coop::scene::Pose>(p.pose); s.act=p.act; s.walk=p.walk;
    s.viaSet=p.viaSet; s.via={p.via.x,p.via.y,p.via.z}; s.seats=p.seats;
    for(int i=0;i<2;++i) s.seat[i]={p.seat[i].x,p.seat[i].y,p.seat[i].z};
    return s;
}
void Store(const coop::scene::Spot& s, float at) {
    current.ground={s.ground.x,s.ground.y,s.ground.z}; current.heading=s.heading;
    current.pose=uint8_t(s.pose); current.act=s.act; current.walk=s.walk; current.at=at;
    current.viaSet=s.viaSet; current.via={s.via.x,s.via.y,s.via.z}; current.seats=s.seats;
    for(int i=0;i<2;++i) current.seat[i]={s.seat[i].x,s.seat[i].y,s.seat[i].z};
}
void Restore() {
    coop::scene::ReleaseAll();
    DeleteDecorRides();
    for(auto& [id,s] : saved) {
        auto p=CNetworkPlayerManager::GetPlayer(id);
        if(!p || !p->m_pPed || CPools::GetPed(s.handle)!=p->m_pPed) continue;
        p->m_pPed->m_nPedFlags.bDontAcceptIKLookAts=s.dontAcceptLookAts;
        p->m_pPed->m_nAreaCode=s.area;
        p->m_pPed->m_bIsVisible=s.visible;
        CWorld::Remove(p->m_pPed);
        p->m_pPed->SetPosn(s.pos);
        CWorld::Add(p->m_pPed);
        p->m_pPed->UpdateRwMatrix(); p->m_pPed->UpdateRwFrame();
    }
    saved.clear(); haveSpot=false;
}
void Send() {
    ++current.sequence; current.sender=CNetworkPlayerManager::m_localPlayerId; current.kind=0;
    GetPacketFactory().Send(current);
    lastSend=GetTickCount();
}
void SendReady() {
    if (!current.session || !SameScene(current) || GetTickCount()-lastReady<250) return;
    Snapshot p=current; p.kind=1; p.sender=CNetworkPlayerManager::m_localPlayerId;
    GetPacketFactory().Send(p); lastReady=GetTickCount();
}

// --- scripted scenes (bars, fixed camera; no cutscene file): guests stay in the picture -----------------------------
// Each guest's own game places its CJ beside the principal (the copies everywhere else follow by the normal sync, so
// every screen shows the same). A copy is hidden only while it stands between the camera and the principal, which every
// game sees alike; that guest's own game moves him first if it can. Interruptor scene-guests-visible.
struct SceneGuest {
    int handle = -1;
    CPed* ped = nullptr;
    CVector saved{};
    float savedHeading = 0.0f;
    unsigned int savedArea = 0;
    bool savedVisible = true, hidden = false, placed = false, walking = false;
    CVector spot{}, anchor{};
    float heading = 0.0f;
    uint32_t lastTry = 0;
    const char* why = "";
};
std::map<playerid_t, SceneGuest> sceneGuests;
bool sceneHeld = false;
uint32_t sceneStart = 0;
int sceneMission = -1;

bool IsGuestOf(playerid_t id) { return id.valid() && id != Owner(); }
int GuestRank(playerid_t id) {
    int rank = 0;
    auto counts = [&](playerid_t other) { if (IsGuestOf(other) && other < id) ++rank; };
    counts(CNetworkPlayerManager::m_localPlayerId);
    for (auto p : CNetworkPlayerManager::m_pPlayers)
        if (p->m_pPed && !p->m_bDead && CCoopMission::ShouldIncludeNetworkPlayer(p)) counts(p->m_playerId);
    return rank;
}
CVector Chest(CPed* ped) { return ped->GetPosition() + CVector{0.0f, 0.0f, 0.35f}; }
// Between the camera and the principal (or right at the lens): the one reason left to hide a guest.
bool CoversCamera(const CVector& chest, const CVector& target) {
    CVector cam = TheCamera.GetPosition();
    CVector seg = target - cam;
    float len2 = seg.x * seg.x + seg.y * seg.y + seg.z * seg.z;
    CVector rel = chest - cam;
    if (rel.Magnitude() < 1.2f) return true;
    if (len2 < 0.01f) return false;
    float t = (rel.x * seg.x + rel.y * seg.y + rel.z * seg.z) / len2;
    if (t <= 0.02f || t >= 0.98f) return false;
    return (rel - seg * t).Magnitude() < 0.85f;
}
float HeadingTo(const CVector& from, const CVector& to) {
    return std::atan2(-(to.x - from.x), to.y - from.y) * 57.29578f;  // SA heading: 0 = north, degrees
}
// gta-reversed World.cpp: FindGroundZFor3DCoord (0x5696C0), TestSphereAgainstWorld (0x569E20),
// GetIsLineOfSightClear (0x56A490). Spots at the principal's sides and behind, never in front of him.
bool FindSceneSpot(CPed* me, CPed* principal, int rank, CVector& out, float& heading, const char*& why) {
    CEntity* base = principal;
    float side = 1.3f, back = 0.0f, refFeet = principal->GetPosition().z - 1.0f, tolDown = 1.2f;
    CVehicle* car = principal->m_nPedFlags.bInVehicle ? principal->m_pVehicle : nullptr;
    if (car && CPools::ms_pVehiclePool->IsObjectValid(car)) {
        if (car->m_vecMoveSpeed.Magnitude() * 50.0f > 2.0f) { why = "host-car-moving"; return false; }
        base = car;
        auto* col = car->GetColModel();
        float halfW = col ? (col->m_boundBox.m_vecMax.x - col->m_boundBox.m_vecMin.x) * 0.5f : 1.0f;
        float halfL = col ? (col->m_boundBox.m_vecMax.y - col->m_boundBox.m_vecMin.y) * 0.5f : 2.5f;
        side = halfW + 0.9f; back = halfL + 1.1f;
        refFeet = car->GetPosition().z - 0.6f; tolDown = 2.0f;
    } else car = nullptr;
    if (!base->m_matrix) { why = "no-matrix"; return false; }
    CVector origin = base->GetPosition(), right = base->GetRight(), fwd = base->GetForward();
    right.z = 0.0f; fwd.z = 0.0f; right.Normalise(); fwd.Normalise();
    // (right, forward) in metres from the principal, or from the car's centre; `edge` = where the line of sight starts
    struct Cand { float r, f, er, ef; };
    Cand list[16]; int count = 0;
    if (car) {
        float halfW = side - 0.9f, halfL = back - 1.1f;
        for (float f : {0.0f, -0.5f * halfL, 0.4f * halfL})
            for (float s : {1.0f, -1.0f}) list[count++] = {s * side, f, s * (halfW + 0.25f), f};
        for (float r : {0.0f, 0.6f * halfW, -0.6f * halfW}) list[count++] = {r, -back, r, -(halfL + 0.25f)};
    } else {
        const float ped[][2] = {{1.3f,-0.4f},{-1.3f,-0.4f},{1.0f,-1.4f},{-1.0f,-1.4f},{2.2f,-0.2f},{-2.2f,-0.2f},
            {1.8f,-2.0f},{-1.8f,-2.0f},{0.0f,-1.9f},{2.6f,0.7f},{-2.6f,0.7f},{3.2f,-1.2f},{-3.2f,-1.2f}};
        for (const auto& c : ped) list[count++] = {c[0], c[1], 0.0f, 0.0f};
    }
    CVector target = car ? origin : Chest(principal);
    CVector camFwd = TheCamera.GetForward(), cam = TheCamera.GetPosition();
    bool haveAny = false;
    CVector any{};
    why = "no-safe-spot";
    for (int k = 0; k < count; ++k) {
        const Cand& c = list[(k + rank * 2) % count];
        CVector p = origin + right * c.r + fwd * c.f;
        CVector from = car ? origin + right * c.er + fwd * c.ef + CVector{0.0f, 0.0f, 0.3f} : Chest(principal);
        bool found = false;
        float z = CWorld::FindGroundZFor3DCoord(p.x, p.y, refFeet + 1.5f, &found, nullptr);
        if (!found || z > refFeet + 1.0f || z < refFeet - tolDown) continue;
        p.z = z;
        CVector chest{p.x, p.y, z + 1.35f};
        if (CWorld::TestSphereAgainstWorld(CVector{p.x, p.y, z + 1.0f}, 0.45f, me, true, true, true, true, true, false)) continue;
        if (!CWorld::GetIsLineOfSightClear(from, chest, true, true, false, true, false, false, false)) continue;
        if (CoversCamera(chest, target)) continue;
        CVector toSpot = chest - cam;
        if (toSpot.x * camFwd.x + toSpot.y * camFwd.y + toSpot.z * camFwd.z > 0.5f) {
            out = p; heading = HeadingTo(p, origin + fwd * 2.0f);
            return true;  // in the shot
        }
        if (!haveAny) { haveAny = true; any = p; }
    }
    if (!haveAny) return false;
    out = any; heading = HeadingTo(any, origin + fwd * 2.0f);
    return true;
}
void SetSceneHidden(SceneGuest& g, CPed* ped, bool hide, playerid_t id, const char* why) {
    if (hide == g.hidden) { if (hide) ped->m_bIsVisible = false; return; }
    if (hide) { g.savedVisible = ped->m_bIsVisible; ped->m_bIsVisible = false; }
    else ped->m_bIsVisible = g.savedVisible;
    g.hidden = hide;
    CMissionMirror::LogEventf("scene-guest-%s player=%u mission=%d reason=%s", hide ? "hide" : "show", id.value(), sceneMission, why);
}
SceneGuest& TrackGuest(playerid_t id, CPed* ped) {
    auto& g = sceneGuests[id];
    if (g.ped != ped || CPools::GetPed(g.handle) != ped) {
        g = SceneGuest{};
        g.ped = ped; g.handle = CPools::GetPedRef(ped);
        g.saved = ped->GetPosition(); g.savedHeading = ped->m_fCurrentRotation * 57.29578f; g.savedArea = ped->m_nAreaCode;
        g.savedVisible = ped->m_bIsVisible;
    }
    return g;
}
void PlaceLocalGuest(CPed* me, CPed* principal, uint32_t now) {
    playerid_t id = CNetworkPlayerManager::m_localPlayerId;
    auto& g = TrackGuest(id, me);
    auto keep = [&](const char* why) {
        if (strcmp(g.why, why)) { g.why = why; CMissionMirror::LogEventf("scene-guest-keep player=%u mission=%d reason=%s", id.value(), sceneMission, why); }
    };
    if (me->m_fHealth <= 0.0f) return keep("dead");
    if (me->m_nPedFlags.bInVehicle) {
        // The principal got out of the mission vehicle for the scene (Home Invasion: CJ and Ryder walk into the garage
        // and the van is deleted): the guest stays in his seat, i.e. "inside", and never takes part. He gets out of a
        // mission vehicle the principal is no longer in, and then follows the normal placement (2026-11-12).
        CVehicle* car = me->m_pVehicle;
        if (!principal->m_nPedFlags.bInVehicle && car && car->m_nCreatedBy == MISSION_VEHICLE && now - sceneStart >= 400 &&
            car->m_vecMoveSpeed.Magnitude() * 50.0f < 2.0f) {
            Command<Commands::REMOVE_CHAR_FROM_CAR_MAINTAIN_POSITION>(CPools::GetPedRef(me), CPools::GetVehicleRef(car));
            CMissionMirror::LogEventf("scene-guest-exit-vehicle player=%u mission=%d", id.value(), sceneMission);
            g.lastTry = 0;
        }
        return keep("in-vehicle");  // seen in his own seat, as everywhere
    }
    // SWEET1 (Tagging Up Turf, 2026-11-18): the tag dialogs play while the players spray apart; nobody is brought
    // into the picture (the placement pulled a spraying guest 46 and 71 m onto the roofs). The scene reaches him
    // mirrored wherever he is.
    if (sceneMission == 13) return keep("sweet1-free");
    // INTRO1 (Big Smoke's house, 2026-11-19): a guest on his way into the mission car is not placed, held or warped by the
    // scene; that clears his task and he loses the seat the boarding gave him
    if (CConfigLoader::ms_bIntroBoard && sceneMission == 11 &&
        CCommandPutNetworkPlayersInCar::IsBoardingThisPlayer(id))
    {
        // the scene may have hidden him while he covered the camera: whoever is boarding has to be visible
        SetSceneHidden(g, me, false, id, "boarding");
        return keep("boarding");
    }
    if (me->m_nAreaCode != principal->m_nAreaCode) return keep("other-area");
    if (!g.placed && DistanceBetweenPoints(me->GetPosition(), principal->GetPosition()) > 150.0f) return keep("far");
    if (now - sceneStart < 400) return;  // the scene's own move of CJ comes over the sync first
    // The guest already walked in next to the principal (Ryder's pizzeria, 2026-11-18): he is in the picture where he
    // stands, and a rank-spot warp only dragged him around the small room. Only a guest farther out is brought in.
    if (!g.placed) {
        const CVector off = me->GetPosition() - principal->GetPosition();
        if (off.x * off.x + off.y * off.y < 10.0f * 10.0f) return keep("near");
    }
    // The principal drives through a short scene (Robbing Uncle Sam: each crate the forklift drops in the truck, 13.11):
    // a guest already nearby is in the picture and keeps doing his part; moving him there was a teleport every crate.
    if (!g.placed && principal->m_nPedFlags.bInVehicle &&
        DistanceBetweenPoints(me->GetPosition(), principal->GetPosition()) < 30.0f &&
        !CoversCamera(Chest(me), Chest(principal)))
        return keep("principal-in-vehicle-near");
    // Robbing Uncle Sam: the principal sits in the depot's forklift (530) for the crate scene, wherever the guests are in
    // the depot: nobody is moved for it (the near test above left the ones farther than 30 m to be warped each crate)
    if (!g.placed && CConfigLoader::ms_bCoopRobbingUncleSam && principal->m_nPedFlags.bInVehicle && principal->m_pVehicle &&
        principal->m_pVehicle->m_nModelIndex == 530 && principal->m_pVehicle->m_nCreatedBy == MISSION_VEHICLE)
        return keep("principal-in-forklift");
    CVector anchor = principal->m_nPedFlags.bInVehicle && principal->m_pVehicle ? principal->m_pVehicle->GetPosition() : principal->GetPosition();
    bool covers = CoversCamera(Chest(me), Chest(principal));
    CVector d = anchor - g.anchor;
    // Only the anchor's move re-places him; covering the camera hides him below instead. Moving him out of the shot
    // re-teleported him every 500 ms in small interiors, where every spot covers (Ryder's pizzeria, 2026-11-18).
    bool need = !g.placed || d.x * d.x + d.y * d.y > 3.5f * 3.5f;
    if (need && now - g.lastTry >= 500) {
        g.lastTry = now;
        CVector spot; float heading = 0.0f; const char* why = "";
        if (FindSceneSpot(me, principal, GuestRank(id), spot, heading, why)) {
            // Where he stands when the scene first moves him is what comes back at the end, not where he was when the
            // scene began (Just Business, 12.11: the guest was outside when the bars came on, a rule then brought him
            // into interior 18, and the end of the scene sent him back outside). Same area as the principal here.
            if (!g.placed) {
                g.saved = me->GetPosition(); g.savedHeading = me->m_fCurrentRotation * 57.29578f; g.savedArea = me->m_nAreaCode;
            }
            CVector delta = spot - me->GetPosition();
            float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            bool walk = g.placed && dist < 12.0f;
            // CJ running in the scene (SMOKE3: CJ and Smoke run, 12.11): he runs too, not walks behind them
            const bool run = principal->m_nMoveState >= PEDMOVE_RUN || dist > 6.0f;
            if (walk) Command<Commands::TASK_GO_STRAIGHT_TO_COORD>(g.handle, spot.x, spot.y, spot.z, run ? 6 : 4, 10000);  // PEDMOVE_RUN / PEDMOVE_WALK
            else {
                Command<Commands::CLEAR_CHAR_TASKS>(g.handle);
                Command<Commands::SET_CHAR_COORDINATES>(g.handle, spot.x, spot.y, spot.z);
                Command<Commands::SET_CHAR_HEADING>(g.handle, heading);
            }
            if (principal->m_nPedFlags.bInVehicle == false)
                Command<Commands::TASK_LOOK_AT_CHAR>(g.handle, CPools::GetPedRef(principal), 8000);
            g.placed = true; g.walking = walk; g.spot = spot; g.anchor = anchor; g.heading = heading; g.why = "";
            CMissionMirror::LogEventf("scene-guest-place player=%u mission=%d rank=%d at=%.1f,%.1f,%.1f %s %.1f m",
                id.value(), sceneMission, GuestRank(id), spot.x, spot.y, spot.z, walk ? (run ? "run" : "walk") : "warp", dist);
            covers = false;
        } else keep(why);
    }
    if (g.walking) {
        CVector rest = g.spot - me->GetPosition();
        if (rest.x * rest.x + rest.y * rest.y < 0.6f * 0.6f) { g.walking = false; Command<Commands::SET_CHAR_HEADING>(g.handle, g.heading); }
    }
    SetSceneHidden(g, me, covers, id, "covers-camera");
}
void ReleaseSceneGuests() {
    for (auto& [id, g] : sceneGuests) {
        CPed* ped = CPools::GetPed(g.handle);
        if (!ped || ped != g.ped) continue;
        if (g.hidden) ped->m_bIsVisible = g.savedVisible;
        // never back into another interior/outside than the one he is in now (the area is not part of the coordinates)
        if (CConfigLoader::ms_bSceneGuestEndNearHost) {
            // The scene end leaves the guest next to the authority, in the safe spot around him (Home Invasion, 2026-11-12: the
            // old position was taken after the mission's gather had put him inside the closed garage, and the end of the scene
            // sent him back in). Not when something else moved him since (a rule's teleport) or when he is already near him and can
            // see him (Catalyst, 2026-11-12: 4 m teleported a guest standing a few metres away after every short scene).
            if (id == CNetworkPlayerManager::m_localPlayerId && g.placed && ped->m_fHealth > 0.0f && !ped->m_nPedFlags.bInVehicle) {
                auto owner = CNetworkPlayerManager::GetPlayer(Owner());
                CPed* principal = owner ? owner->m_pPed : nullptr;
                CVector off = ped->GetPosition() - g.spot;
                if (principal && !owner->m_bDead && principal->m_fHealth > 0.0f && principal->m_nAreaCode == ped->m_nAreaCode &&
                    off.x * off.x + off.y * off.y < (g.walking ? 144.0f : 4.0f)) {
                    CVector to = ped->GetPosition() - principal->GetPosition();
                    const bool beside = to.x * to.x + to.y * to.y < 25.0f * 25.0f &&
                        CWorld::GetIsLineOfSightClear(ped->GetPosition(), principal->GetPosition(), true, false, false, true, false, false, false);
                    if (!beside) {
                        const int rank = (std::min)(GuestRank(id), 2);
                        CVector spots[3];
                        CCommandTeleportPlayersToHostSafely::FindSafeSpots(principal->GetPosition(), principal->m_fCurrentRotation, rank + 1, spots);
                        CVector at = spots[rank];
                        if (at.x == 0.0f && at.y == 0.0f && at.z == 0.0f) at = principal->GetPosition();
                        Command<Commands::CLEAR_CHAR_TASKS>(g.handle);
                        Command<Commands::SET_CHAR_COORDINATES>(g.handle, at.x, at.y, at.z);
                        CMissionMirror::LogEventf("scene-guest-end-near-host player=%u mission=%d at=%.1f,%.1f,%.1f", id.value(), sceneMission, at.x, at.y, at.z);
                    }
                }
            }
            continue;
        }
        if (id == CNetworkPlayerManager::m_localPlayerId && g.placed && ped->m_fHealth > 0.0f && !ped->m_nPedFlags.bInVehicle &&
            ped->m_nAreaCode == g.savedArea) {
            CVector off = ped->GetPosition() - g.spot;
            // moved by somebody else meanwhile (a rule's teleport, the end of the scene): that stays
            if (off.x * off.x + off.y * off.y < (g.walking ? 144.0f : 4.0f)) {
                Command<Commands::CLEAR_CHAR_TASKS>(g.handle);
                Command<Commands::SET_CHAR_COORDINATES>(g.handle, g.saved.x, g.saved.y, g.saved.z - ped->GetDistanceFromCentreOfMassToBaseOfModel());
                Command<Commands::SET_CHAR_HEADING>(g.handle, g.savedHeading);
                CMissionMirror::LogEventf("scene-guest-restore player=%u mission=%d", id.value(), sceneMission);
            }
        }
    }
    sceneGuests.clear();
    sceneHeld = false;
}
void ProcessSceneGuests(CPed* principal, int mission) {
    uint32_t now = GetTickCount();
    if (!sceneHeld) {
        sceneHeld = true; sceneStart = now; sceneMission = mission;
        CMissionMirror::LogEventf("scene-guests-visible mission=%d", mission);
    }
    auto* me = FindPlayerPed(0);
    if (me && IsGuestOf(CNetworkPlayerManager::m_localPlayerId) && !CCoopMission::IsScriptAuthority())
        PlaceLocalGuest(me, principal, now);
    for (auto p : CNetworkPlayerManager::m_pPlayers) {
        auto* ped = p->m_pPed;
        if (!ped || p->m_bDead || !IsGuestOf(p->m_playerId) || !CCoopMission::ShouldIncludeNetworkPlayer(p) ||
            ped->m_nPedFlags.bInVehicle || ped->m_nAreaCode != principal->m_nAreaCode) {
            auto it = sceneGuests.find(p->m_playerId);
            if (it != sceneGuests.end() && it->second.hidden && ped == it->second.ped) SetSceneHidden(it->second, ped, false, p->m_playerId, "left");
            continue;
        }
        auto& g = TrackGuest(p->m_playerId, ped);
        // cutscene-show-others off (F1 "Un solo CJ"): the other players are not shown in this screen's scenes either
        if (!CConfigLoader::ms_bCutsceneShowOthers) SetSceneHidden(g, ped, true, p->m_playerId, "single-cj");
        else SetSceneHidden(g, ped, CoversCamera(Chest(ped), Chest(principal)), p->m_playerId, "covers-camera");
    }
}
}

// gta-reversed app_game.cpp Idle: before visibility enumeration, with the frame's camera.
static void __cdecl ConstructRenderListWithPartners() {
    CCutscenePartners::Render();
    plugin::Call<0x5556E0>();
}

void CCutscenePartners::Init() {
    const unsigned char expected[] = {0xE8,0xE2,0x6C,0x01,0x00};
    if(!memcmp(reinterpret_cast<void*>(0x53E9F9),expected,sizeof expected))
        patch::RedirectCall(0x53E9F9,ConstructRenderListWithPartners);
    else CMissionMirror::LogEvent("cutscene-partners render call differs; hook disabled");
}

bool CCutscenePartners::ControlsPed(CPed* ped) {
    if (!ped || !Eligible() || (!scriptedHeld && (!running || !SameScene(current)))) return false;
    for (const auto& [id, state] : saved)
        if (CPools::GetPed(state.handle)==ped) return true;
    return false;
}

void CCutscenePartners::Reset() {
    Restore(); current=Snapshot{}; ready.clear(); prepared=false; running=false;
    preparedName[0]=0; lastSequence=lastReceive=lastReady=0;
    scriptedHeld=false;
    if (sceneHeld) ReleaseSceneGuests();
    if (!CNetwork::m_bAuthenticated) { beforeMission.clear(); entryRides.clear(); observedMission=-1; }
}

void CCutscenePartners::Process() {
    // Start extraction before a mission; load only below the temporary cutscene block.
    if(CConfigLoader::ms_bCutscenePartners && CNetwork::m_bAuthenticated) coop::scene::ProcessMocap();
    if (CConfigLoader::ms_bCutscenePartners && CNetwork::m_bAuthenticated) ObserveEntryRides();
    if (!Eligible()) { if(prepared || running || current.session || scriptedHeld || sceneHeld) Reset(); return; }
    if(current.owner.valid() && current.owner!=Owner()) Reset();
    const bool loaded=CCutsceneMgr::ms_cutsceneLoadStatus != 0 && CCutsceneMgr::ms_cutsceneName[0];
    if(loaded && (!prepared || _strnicmp(preparedName,CCutsceneMgr::ms_cutsceneName,8))) {
        if(prepared && _strnicmp(preparedName,CCutsceneMgr::ms_cutsceneName,8)) Reset();
        Restore(); prepared=true; strncpy_s(preparedName,CCutsceneMgr::ms_cutsceneName,8);
        coop::scene::Prepare(preparedName);
        if (!_strnicmp(preparedName,"SWEET2B",8)) {
            for (const auto& [id, ride] : entryRides)
                if (ride.model >= 0) CStreaming::RequestModel(ride.model, 0);
            CStreaming::LoadAllRequestedModels(false);
        }
    }
    if(!CCutsceneMgr::ms_running) {
        if(running) {
            if(CCoopMission::IsScriptAuthority() && current.session) { current.active=false; Send(); }
            Reset();
        } else if(!loaded && prepared) Reset();
        return;
    }
    if(!running) {
        running=true;
        if(CCoopMission::IsScriptAuthority()) {
            current=Snapshot{}; current.owner=Owner(); current.session=++nextSession;
            if(!current.session) current.session=++nextSession;
            strncpy_s(current.scene,CCutsceneMgr::ms_cutsceneName,8); Send();
        }
    }
    if(!CCoopMission::IsScriptAuthority()) {
        auto authority=CNetworkPlayerManager::GetPlayer(Owner());
        if(!authority || authority->m_bDead || !CCoopMission::ShouldIncludeNetworkPlayer(authority)) { Reset(); return; }
        SendReady();
        if(lastReceive && GetTickCount()-lastReceive>2000) { Restore(); current=Snapshot{}; }
    }
}

void CCutscenePartners::Render() {
    bool scripted = false;
    int mission = MissionId();
    auto* local = FindPlayerPed(0);
    auto owner = CNetworkPlayerManager::GetPlayer(Owner());
    auto* principal = CCoopMission::IsScriptAuthority() ? local : owner ? owner->m_pPed : nullptr;
    if (Eligible() && !CCutsceneMgr::ms_running && principal && TheCamera.m_bWideScreenOn &&
        (principal->m_nAreaCode == 0 || (mission == 30 && CConfigLoader::ms_bCoopJustBusiness))) {
        auto pos = principal->GetPosition();
        scripted = (mission == 14 && DistanceBetweenPoints(pos, CVector(2517.5f, -1678.5f, 13.4f)) < 12.0f) ||
            (mission == 11 && DistanceBetweenPoints(pos, CVector(2498.4f, -1689.2f, 14.2f)) < 130.0f) ||
            (mission == 17 && CConfigLoader::ms_bCoopDriveBy) ||
            (mission == 20 && CConfigLoader::ms_bCoopLosSepulcros) ||
            (mission == 28 && CConfigLoader::ms_bCoopRunningDog) ||
            (mission == 29 && CConfigLoader::ms_bCoopWrongSideTracks) ||
            (mission == 30 && CConfigLoader::ms_bCoopJustBusiness);
    }
    // general (every mission): a scene the synced mission script directs with its bars on
    const bool visibleScene = CConfigLoader::ms_bSceneGuestsVisible && Eligible() && !CCutsceneMgr::ms_running &&
        principal && (scripted || CCameraMirror::IsMissionSceneWide());
    if (visibleScene) {
        if (scriptedHeld) { Restore(); scriptedHeld=false; }
        ProcessSceneGuests(principal, mission);
        return;
    }
    if (sceneHeld) ReleaseSceneGuests();
    if (scripted) {
        if (!scriptedHeld) { Restore(); scriptedHeld=true; CMissionMirror::LogEventf("scene-script-rule mission=%d",mission); }
        std::set<playerid_t> included;
        for (auto p : CNetworkPlayerManager::m_pPlayers) {
            auto* ped = p->m_pPed;
            if (!ped || p->m_bDead || !CCoopMission::ShouldIncludeNetworkPlayer(p) ||
                ped->m_nAreaCode != principal->m_nAreaCode ||
                (!saved.count(p->m_playerId) && DistanceBetweenPoints(ped->GetPosition(),principal->GetPosition()) > 150.0f)) continue;
            // The authority already occupies its native seat in scripted scenes.
            if ((mission == 11 || mission == 17 || mission == 20 || mission == 28 || mission == 29 || mission == 30) && p->m_playerId == Owner()) continue;
            if (saved.count(p->m_playerId) && CPools::GetPed(saved.at(p->m_playerId).handle) != ped) { Restore(); scriptedHeld=false; return; }
            included.insert(p->m_playerId);
            int rank = CNetworkPlayerManager::m_localPlayerId != Owner() && CNetworkPlayerManager::m_localPlayerId < p->m_playerId ? 1 : 0;
            for (auto other : CNetworkPlayerManager::m_pPlayers)
                if (other->m_playerId != Owner() && other->m_playerId < p->m_playerId && other->m_pPed &&
                    !other->m_bDead && CCoopMission::ShouldIncludeNetworkPlayer(other)) ++rank;
            if (!saved.count(p->m_playerId)) saved.emplace(p->m_playerId,Saved{CPools::GetPedRef(ped),ped->GetPosition(),ped->m_nAreaCode,bool(ped->m_bIsVisible),bool(ped->m_nPedFlags.bDontAcceptIKLookAts)});
            auto pos=ped->GetPosition();
            coop::scene::Spot hidden{{pos.x,pos.y,pos.z},0,coop::scene::POSE_HIDDEN,0};
            bool seated=false;
            if (mission == 11 && principal->m_nPedFlags.bInVehicle && principal->m_pVehicle &&
                principal->m_pVehicle->m_nModelIndex == 404 && principal->m_pVehicle->m_matrix) {
                auto* car=principal->m_pVehicle;
                int freeSeat=-1, available=0;
                for (int i=0;i<std::min<int>(car->m_nMaxPassengers,3);++i)
                {
                    bool occupied = car->m_apPassengers[i] != nullptr;
                    for (auto other : CNetworkPlayerManager::m_pPlayers)
                        if (other->m_playerId != Owner() && other->m_pPed == car->m_apPassengers[i] &&
                            !other->m_bDead && CCoopMission::ShouldIncludeNetworkPlayer(other)) occupied=false;
                    if (!occupied && available++ == rank) { freeSeat=i; break; }
                }
                if (freeSeat>=0) {
                    auto seat=Seat(car,freeSeat);
                    ped->m_nPedFlags.bDontAcceptIKLookAts=true;
                    coop::scene::HoldVehicleSeat(reinterpret_cast<scene_game::CPed*>(ped),reinterpret_cast<scene_game::CEntity*>(car),{seat.x,seat.y,seat.z});
                    seated=true;
                }
            }
            if (!seated) coop::scene::Hold(reinterpret_cast<scene_game::CPed*>(ped),hidden);
        }
        for (const auto& [id, state] : saved) if (!included.count(id)) { Restore(); scriptedHeld=false; break; }
        return;
    }
    if (scriptedHeld) { Restore(); scriptedHeld=false; }

    if(!Eligible() || !CCutsceneMgr::ms_running || !running) return;
    if(CCoopMission::IsScriptAuthority()) {
        coop::scene::Analyse();
        coop::scene::Spot s; float at;
        bool changed=coop::scene::TakeDecision(s,at);
        if(changed) { Store(s,at); ++current.shot; haveSpot=true; }
        playerid_t ids[3]{}; int count=0;
        for(auto p:CNetworkPlayerManager::m_pPlayers)
            if(p->m_pPed && !p->m_bDead && CCoopMission::ShouldIncludeNetworkPlayer(p) &&
                ready.count(p->m_playerId.value()) && GetTickCount()-ready[p->m_playerId.value()]<1500 && count<3) ids[count++]=p->m_playerId;
        std::sort(ids,ids+count);
        bool rosterChanged=count!=current.count;
        for(int i=0;i<count;++i) rosterChanged |= ids[i]!=current.partners[i];
        if(rosterChanged && !saved.empty()) Restore();
        current.count=uint8_t(count);
        for(int i=0;i<count;++i) current.partners[i]=ids[i];
        if(changed || rosterChanged || GetTickCount()-lastSend>=250) Send();
        if(!haveSpot) return;
    } else {
        if(!current.session || !current.active || !SameScene(current) || !haveSpot) return;
        coop::scene::Follow();
    }
    coop::scene::Spot s;
    if(!coop::scene::CurrentSpot(s)) {
        coop::scene::Spot hidden; hidden.pose=coop::scene::POSE_HIDDEN;
        for(auto& [id,state]:saved) {
            auto p=CNetworkPlayerManager::GetPlayer(id);
            if(p && p->m_pPed && CPools::GetPed(state.handle)==p->m_pPed)
                coop::scene::Hold(reinterpret_cast<scene_game::CPed*>(p->m_pPed),hidden);
        }
        return;
    }
    // T48 keeps the viewer's own CJ: in that viewer's extra slot, show the owner instead.
    // cutscene-show-others off: the copies are still held (out of the way) but always hidden on this screen.
    coop::scene::Spot hiddenSpot; hiddenSpot.pose=coop::scene::POSE_HIDDEN;
    const bool showOthers=CConfigLoader::ms_bCutsceneShowOthers;
    std::set<playerid_t> included;
    for(int rank=0;rank<current.count;++rank) {
        auto id=current.partners[rank];
        if(id==CNetworkPlayerManager::m_localPlayerId) id=current.owner;
        auto p=CNetworkPlayerManager::GetPlayer(id);
        if(!p || !p->m_pPed || p->m_bDead || !CCoopMission::ShouldIncludeNetworkPlayer(p)) continue;
        included.insert(id);
        auto ped=p->m_pPed;
        if(saved.count(id) && CPools::GetPed(saved.at(id).handle)!=ped) { Restore(); return; }
        if(!saved.count(id)) saved.emplace(id,Saved{CPools::GetPedRef(ped),ped->GetPosition(),ped->m_nAreaCode,bool(ped->m_bIsVisible),bool(ped->m_nPedFlags.bDontAcceptIKLookAts)});
        ped->m_nPedFlags.bDontAcceptIKLookAts=true;
        if (auto local = FindPlayerPed(0)) ped->m_nAreaCode=local->m_nAreaCode;
        else { Restore(); return; }
        if (!_strnicmp(CCutsceneMgr::ms_cutsceneName,"SWEET2B",8)) {
            auto slot=current.partners[rank];
            if (!showOthers || !DecorateRide(slot,ped,rank)) {
                coop::scene::Spot hidden; hidden.pose=coop::scene::POSE_HIDDEN;
                coop::scene::Hold(reinterpret_cast<scene_game::CPed*>(ped),hidden);
            }
        } else coop::scene::Hold(reinterpret_cast<scene_game::CPed*>(ped),showOthers ? coop::scene::ForRank(s,rank) : hiddenSpot);
    }
    for(auto& [id,state]:saved) if(!included.count(id)) { Restore(); break; }
}

void CCutscenePartners::HandlePacket(const Snapshot& p) {
    if(!Eligible() || p.owner!=Owner() || p.scene[8]) return;
    if(p.kind==1) {
        if(!CCoopMission::IsScriptAuthority() || p.session!=current.session || !SameScene(p)) return;
        auto player=CNetworkPlayerManager::GetPlayer(p.sender);
        if(player && player->m_pPed && !player->m_bDead && CCoopMission::ShouldIncludeNetworkPlayer(player))
            {
            auto local=FindPlayerPed(0);
            // Readiness is accepted near the owner, before Hold moves this remote copy.
            if(ready.count(p.sender.value()) || (local && local->m_nAreaCode==player->m_pPed->m_nAreaCode &&
                (local->GetPosition()-player->m_pPed->GetPosition()).Magnitude()<120.0f))
                ready[p.sender.value()]=GetTickCount();
        }
        return;
    }
    if(CCoopMission::IsScriptAuthority() || p.sender!=p.owner || !SameScene(p)) return;
    if(current.session && p.session<current.session) return;
    if(p.session==current.session && p.sequence<=lastSequence) return;
    if(p.session!=current.session) { Restore(); lastSequence=0; }
    const bool newShot=!haveSpot || p.shot!=current.shot;
    current=p; lastReceive=GetTickCount(); lastSequence=p.sequence;
    if(!p.active) { Reset(); return; }
    bool member=false;
    for(int i=0;i<p.count;++i) member |= p.partners[i]==CNetworkPlayerManager::m_localPlayerId;
    if(!member) { Restore(); return; }
    if(!p.shot) return;
    if(newShot) coop::scene::FollowSpot(Spot(p),p.at);
    haveSpot=true;
}
