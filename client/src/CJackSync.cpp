#include "stdafx.h"
#include "CJackSync.h"
#include "CMissionMirror.h"

#include <algorithm>
#include <CCarEnterExit.h>
#include <CDamageManager.h>
#include <CTaskComplexEnterCar.h>
#include <CTaskComplexEnterCarAsPassenger.h>
#include <CTaskSimpleCarSetPedOut.h>
#include <CTaskSimpleRunAnim.h>
#include <eAnimations.h>
#include <unordered_map>
#include <vector>

namespace
{
// how long the in-car jacked animation plays before the local victim is actually placed outside
constexpr uint32_t LOCAL_JACK_ANIM_MS = 900;

// player-jack-native: the game's own dragged out task takes the victim out; if it is still inside after this
// long, the old forced exit runs
constexpr uint32_t NATIVE_JACK_TIMEOUT_MS = 5000;

// if a dragged out ped is still inside this long after the jack started, fall back to the old raw warp
constexpr uint32_t PED_JACK_TIMEOUT_MS = 6000;

// C3: the STDCARAMIMS block is requested as streaming model 25575 + block index (same as CParachuteSync)
constexpr int ANIM_BLOCK_MODEL_BASE = 25575;

// C3: how long we keep a reference on that block so it is not freed while the animation plays
constexpr uint32_t JACKED_ANIM_BLOCK_REF_MS = 5000;

struct SLocalJack
{
    bool pending = false;
    bool native = false;  // the game's dragged out task is taking the victim out
    uint32_t leaveAt = 0;
    vehicleid_t vehicleid{};
    int seatid = -1;
    int door = 0;
};

struct SPedJack
{
    uint32_t startedAt = 0;
    vehicleid_t vehicleid{};
    playerid_t thiefPlayerId{};
    bool bReacted = false;  // the game already got the dragged out event (its decision maker reacts by itself)
};

struct SThiefReplay
{
    playerid_t victim{};
    vehicleid_t vehicle{};
    uint32_t startedAt = 0;
    int seatid = -1;
    int door = 0;
};
std::unordered_map<playerid_t, SThiefReplay> ms_thiefReplays;

CPed* GetJackPlayerPed(playerid_t id)
{
    if (id == CNetworkPlayerManager::m_localPlayerId)
        return FindPlayerPed(0);
    auto player = CNetworkPlayerManager::GetPlayer(id);
    return player != nullptr ? player->m_pPed : nullptr;
}

CTaskComplexEnterCar* GetReplayTask(playerid_t thiefId, const SThiefReplay& replay)
{
    auto thief = GetJackPlayerPed(thiefId);
    auto vehicle = CNetworkVehicleManager::GetVehicle(replay.vehicle);
    if (thief == nullptr || !thief->IsVTableValid() || !thief->IsAlive() ||
        vehicle == nullptr || vehicle->m_pVehicle == nullptr || !vehicle->m_pVehicle->IsVTableValid() ||
        thief->m_pIntelligence == nullptr)
        return nullptr;
    auto task = thief->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[TASK_PRIMARY_PRIMARY];
    if (task == nullptr || (task->GetTaskType() != TASK_COMPLEX_ENTER_CAR_AS_DRIVER &&
                              task->GetTaskType() != TASK_COMPLEX_ENTER_CAR_AS_PASSENGER))
        return nullptr;
    auto enter = static_cast<CTaskComplexEnterCar*>(task);
    return enter->m_pTargetVehicle == vehicle->m_pVehicle ? enter : nullptr;
}

bool IsReplayValid(playerid_t thiefId, const SThiefReplay& replay)
{
    auto thief = GetJackPlayerPed(thiefId);
    auto victim = GetJackPlayerPed(replay.victim);
    return CNetwork::m_bAuthenticated && CConfigLoader::ms_bPlayerJackNative &&
        thief != nullptr && thief->IsVTableValid() && thief->IsAlive() &&
        victim != nullptr && victim->IsVTableValid() && victim->IsAlive() &&
        thief->m_nAreaCode == victim->m_nAreaCode && GetReplayTask(thiefId, replay) != nullptr;
}

SLocalJack ms_localJack;
std::unordered_map<playerid_t, uint32_t> ms_warpSuppressedUntil;
std::unordered_map<pedid_t, uint32_t> ms_pedWarpSuppressedUntil;
std::unordered_map<pedid_t, SPedJack> ms_pendingPedJacks;

// C3: the STDCARAMIMS block must be loaded and referenced while a jacked animation plays; each play schedules a
// release so the block can be freed again when the animation is long over
int ms_nJackedAnimBlock = -1;
std::vector<uint32_t> ms_jackedAnimBlockReleaseAt;

// last seat the local player was in (the dragged out task can start after the game cleared the vehicle pointer)
vehicleid_t ms_lastVehicleId{};
uint32_t ms_lastVehicleAt = 0;
int ms_lastSeatId = -1;
int ms_lastDoor = 0;

bool ms_bSentBeingJacked = false;
CTask* ms_lastPassengerJackTask = nullptr;

// the ped the local player's enter car task is dragging out (PED_JACKED is sent once per drag)
CPed* ms_pLastDraggedPed = nullptr;

bool IsBeingDraggedOutTask(int taskType)
{
    switch (taskType)
    {
    case TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT:
    case TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT_AND_STAND_UP:
    case TASK_COMPLEX_CAR_QUICK_BE_DRAGGED_OUT:
    case TASK_SIMPLE_CAR_QUICK_BE_DRAGGED_OUT:
    case TASK_SIMPLE_CAR_SET_PED_QUICK_DRAGGED_OUT:
    case TASK_SIMPLE_CAR_SLOW_BE_DRAGGED_OUT:
    case TASK_SIMPLE_CAR_SET_PED_SLOW_DRAGGED_OUT:
    case TASK_SIMPLE_BIKE_JACKED:
        return true;
    default:
        return false;
    }
}

// accepts both the enter car target door (0, 8, 9, 11) and an eDoors value
int SanitizeDoor(int door)
{
    switch (door)
    {
    case 8:   // TARGET_DOOR_FRONT_RIGHT
    case 9:   // TARGET_DOOR_REAR_RIGHT
    case 11:  // TARGET_DOOR_REAR_LEFT
        return door;
    case DOOR_FRONT_RIGHT:
        return 8;
    case DOOR_REAR_LEFT:
        return 11;
    case DOOR_REAR_RIGHT:
        return 9;
    default:
        return 0;  // TARGET_DOOR_FRONT_LEFT
    }
}

bool IsLeftDoor(int seatid, int door)
{
    // the driver is always pulled out through the left door
    if (seatid < 0)
        return door == TARGET_DOOR_FRONT_LEFT || door == TARGET_DOOR_REAR_LEFT;

    return door == TARGET_DOOR_FRONT_LEFT || door == TARGET_DOOR_REAR_LEFT;
}
// C3: the jacked animation belongs to STDCARAMIMS; a client that never entered a car may not have that block loaded
// yet, and then BlendAnimation shows nothing. Load it (don't fail the caller if it can't) and reference it.
bool EnsureJackedAnimBlock()
{
    if (ms_nJackedAnimBlock < 0)
        ms_nJackedAnimBlock = CAnimManager::GetAnimationBlockIndex("STDCARAMIMS");

    if (ms_nJackedAnimBlock < 0)
        return false;

    if (ms_nJackedAnimBlock != 0 && !CAnimManager::ms_aAnimBlocks[ms_nJackedAnimBlock].bLoaded)
    {
        CStreaming::RequestModel(ANIM_BLOCK_MODEL_BASE + ms_nJackedAnimBlock, eStreamingFlags::GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);

        if (!CAnimManager::ms_aAnimBlocks[ms_nJackedAnimBlock].bLoaded)
            return false;
    }

    return true;
}

void PlayJackedTask(CPed* ped, CVehicle* vehicle, int seatid, int door)
{
    if (ped == nullptr || !ped->IsVTableValid() || ped->m_pIntelligence == nullptr)
        return;

    if (seatid >= 0 && vehicle != nullptr && vehicle->IsVTableValid())
        door = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);

    door = SanitizeDoor(door);

    // C3: hold the block while the animation plays; the block is freed again from Process once it is over
    if (EnsureJackedAnimBlock() && ms_nJackedAnimBlock > 0)
    {
        CAnimManager::AddAnimBlockRef(ms_nJackedAnimBlock);
        ms_jackedAnimBlockReleaseAt.push_back(GetTickCount() + JACKED_ANIM_BLOCK_REF_MS);
    }

    int animId = IsLeftDoor(seatid, door) ? ANIM_STDCARAMIMS_CAR_JACKEDLHS : ANIM_STDCARAMIMS_CAR_JACKEDRHS;
    ped->m_pIntelligence->m_TaskMgr.SetTask(
        new CTaskSimpleRunAnim(ANIM_GROUP_STDCARAMIMS, animId, 8.0f, false), TASK_PRIMARY_PRIMARY, false);
}

bool IsPedBeingDraggedOut(CPed* ped)
{
    if (ped == nullptr || !ped->IsVTableValid() || ped->m_pIntelligence == nullptr)
        return false;

    CTaskManager& taskManager = ped->m_pIntelligence->m_TaskMgr;
    for (int type : {TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT_AND_STAND_UP,
             TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT, TASK_SIMPLE_CAR_SLOW_BE_DRAGGED_OUT,
             TASK_COMPLEX_CAR_QUICK_BE_DRAGGED_OUT, TASK_SIMPLE_CAR_QUICK_BE_DRAGGED_OUT,
             TASK_SIMPLE_CAR_SET_PED_QUICK_DRAGGED_OUT, TASK_SIMPLE_CAR_SET_PED_SLOW_DRAGGED_OUT,
             TASK_SIMPLE_BIKE_JACKED})
    {
        if (taskManager.FindActiveTaskByType(type) != nullptr)
            return true;
    }
    return false;
}

// gta-reversed TaskComplexCarSlowBeDraggedOutAndStandUp.cpp: ctor 0x648620 (vehicle, target door), 0x14 bytes
CTask* CreateSlowBeDraggedOutAndStandUpTask(CVehicle* vehicle, int door)
{
    void* task = CTask::operator new(0x14);
    if (task == nullptr)
        return nullptr;

    plugin::CallMethod<0x648620, void*, CVehicle*, int>(task, vehicle, door);
    return static_cast<CTask*>(task);
}

// the task the game gives a jacked ped: bikes have no slow jack (gta-reversed CCarEnterExit::IsCarSlowJackRequired,
// TaskComplexEnterCar.cpp:460) and their anim group has no CAR_JACKEDLHS/RHS, so the car task crashes in
// CAnimManager::UncompressAnimation; TaskComplexEnterCar::CreateNextSubTask uses BikeJacked (ctor 0x648B90) for them,
// and for quads too (the IsBike() || IsSubQuad() branch at 0x63FAA6; quad idea from PR #3 by Lyrax7z)
CTask* CreateJackedVictimTask(CVehicle* vehicle, int seatid, int door)
{
    if (vehicle->m_nVehicleType != VEHICLE_BIKE && vehicle->m_nVehicleType != VEHICLE_BMX &&
        vehicle->m_nVehicleSubType != VEHICLE_QUAD)
        return CreateSlowBeDraggedOutAndStandUpTask(vehicle, door);

    void* bikeTask = CTask::operator new(0x2C);
    if (bikeTask == nullptr)
        return nullptr;
    return plugin::CallMethodAndReturn<CTask*, 0x648B90, void*, CVehicle*, unsigned int, unsigned int, CPed*, bool>(
        bikeTask, vehicle, seatid < 0 ? 10 : 11, 1000, nullptr, seatid < 0);
}

// gta-reversed EventDraggedOutCar.cpp (ctor 0x4AD250, dtor 0x4AD330, 0x20 bytes) and EventGroup.cpp (Add 0x4AB420
// clones it): the event the victim's decision maker answers with flee, fight or stealing the car back
void AddDraggedOutCarEvent(CPed* ped, CVehicle* vehicle, CPed* jacker)
{
    alignas(4) uint8_t event[0x20]{};
    plugin::CallMethod<0x4AD250, void*, CVehicle*, CPed*, bool>(event, vehicle, jacker, vehicle->m_pDriver == ped);
    plugin::CallMethodAndReturn<void*, 0x4AB420, CEventGroup*, void*, bool>(
        &ped->m_pIntelligence->m_eventGroup, event, false);
    plugin::CallMethod<0x4AD330, void*>(event);
}

// what the jacker's CTaskComplexEnterCar::CreateDragPedOutSubTask does to its victim (gta-reversed
// TaskComplexEnterCar.cpp:801): the game itself takes the ped out with its animation and stands it up. Only its
// syncer gets the event (the copies have no AI). Returns whether the game got the event.
bool StartNativeDraggedOut(CNetworkPed* networkPed, CVehicle* vehicle, int seatid, int door, playerid_t thiefPlayerId)
{
    CPed* ped = networkPed->m_pPed;

    // the thief's copy here already ran its own enter car task on it (with the event, on the syncer)
    if (IsPedBeingDraggedOut(ped))
        return true;

    if (seatid >= 0)
        door = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);

    // the native task only takes doors 8-11 (gta-reversed TaskSimpleCarSlowBeDraggedOut.cpp ComputeAnimID 0x648330):
    // with 0 it reads an uninitialized anim id and crashes in BlendAnimation 0x4D4610
    door = SanitizeDoor(door);
    if (door == TARGET_DOOR_FRONT_LEFT)
        door = 10;  // TARGET_DOOR_DRIVER (missing in plugin-sdk)

    CTask* task = CreateJackedVictimTask(vehicle, seatid, door);
    if (task == nullptr)
        return false;

    ped->m_pIntelligence->AddTaskPrimaryMaybeInGroup(task, true);

    if (!networkPed->m_bSyncing)
        return false;

    CNetworkPlayer* thief = CNetworkPlayerManager::GetPlayer(thiefPlayerId);
    CPed* thiefPed = thief != nullptr ? thief->m_pPed : nullptr;
    if (thiefPed == nullptr || !thiefPed->IsVTableValid())
        return false;

    // a copy taken over earlier may still have the AI CreateGamePed neutralizes
    if (static_cast<int>(ped->m_pIntelligence->m_nDecisionMakerType) < 0)
    {
        ped->SetPedDefaultDecisionMaker();
        ped->m_pIntelligence->SetSeeingRange(30.0);
        ped->m_pIntelligence->SetHearingRange(30.0);
    }

    AddDraggedOutCarEvent(ped, vehicle, thiefPed);
    return true;
}

// player-jack-native: a player pulled out by another player gets the same task the game gives the victim of any
// jacker (CTaskComplexCarSlowBeDraggedOutAndStandUp, see StartNativeDraggedOut): pulled out through the door,
// thrown to the ground and standing up, instead of a one-shot animation in the seat and a warp outside.
bool StartNativeDraggedOutPlayer(CPed* ped, CVehicle* vehicle, int seatid, int door)
{
    if (ped == nullptr || !ped->IsVTableValid() || ped->m_pIntelligence == nullptr || vehicle == nullptr ||
        !vehicle->IsVTableValid() || !ped->IsAlive())
        return false;

    ped->m_nPedFlags.CantBeKnockedOffBike = 2;
    if (IsPedBeingDraggedOut(ped))
        return true;

    if (seatid >= 0)
        door = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);

    // the native task only takes doors 8-11 (see StartNativeDraggedOut)
    door = SanitizeDoor(door);
    if (door == TARGET_DOOR_FRONT_LEFT)
        door = 10;  // TARGET_DOOR_DRIVER (missing in plugin-sdk)

    CTask* task = CreateJackedVictimTask(vehicle, seatid, door);
    if (task == nullptr)
        return false;

    // set at once: AddTaskPrimaryMaybeInGroup only queues an event, and the task reads the vehicle of the ped
    // unchecked when it starts (CreateFirstSubTask 0x648A10), so it must start while the ped still sits in it
    ped->m_pIntelligence->m_TaskMgr.SetTask(task, TASK_PRIMARY_PRIMARY, false);
    return true;
}

// C4 fallback (no event could be given): once the ped is out of the car, give it back its normal AI and make it
// flee from its thief
void ApplyJackReaction(CNetworkPed* networkPed, playerid_t thiefPlayerId, CVehicle* vehicle)
{
    if (networkPed == nullptr)
        return;

    CPed* ped = networkPed->m_pPed;
    if (ped == nullptr || !ped->IsVTableValid() || ped->m_pIntelligence == nullptr)
        return;

    // CreateGamePed neutralizes the AI of every network ped; it is out of the car now and must react
    ped->SetPedDefaultDecisionMaker();
    ped->m_pIntelligence->SetSeeingRange(30.0);
    ped->m_pIntelligence->SetHearingRange(30.0);

    // flee the player that pulled it out; if it is not around anymore, flee the car it was pulled from
    CNetworkPlayer* thief = CNetworkPlayerManager::GetPlayer(thiefPlayerId);
    CPed* thiefPed = thief != nullptr ? thief->m_pPed : nullptr;

    if (thiefPed != nullptr && thiefPed->IsVTableValid())
    {
        plugin::Command<Commands::TASK_SMART_FLEE_CHAR>(
            CPools::GetPedRef(ped), CPools::GetPedRef(thiefPed), 30.0f, 5000);
    }
    else if (vehicle != nullptr && vehicle->IsVTableValid())
    {
        CVector pos = vehicle->GetPosition();
        plugin::Command<Commands::TASK_SMART_FLEE_POINT>(
            CPools::GetPedRef(ped), pos.x, pos.y, pos.z, 30.0f, 5000);
    }
}
}  // namespace

void CJackSync::SuppressWarp(playerid_t playerid, uint32_t durationMs)
{
    if (!playerid.valid())
        return;

    ms_warpSuppressedUntil[playerid] = GetTickCount() + durationMs;
}

bool CJackSync::IsPlayerJackInProgress(CPed* ped)
{
    if (!CConfigLoader::ms_bPlayerJackNative || ped == nullptr || !ped->IsVTableValid() || !ped->IsAlive())
        return false;
    if (IsPedBeingDraggedOut(ped))
        return true;
    for (const auto& [thiefId, replay] : ms_thiefReplays)
    {
        if (GetTickCount() - replay.startedAt < 8000 && IsReplayValid(thiefId, replay) &&
            (ped == GetJackPlayerPed(thiefId) ||
                (ped == GetJackPlayerPed(replay.victim) && ped->m_nPedFlags.bInVehicle)))
            return true;
    }
    return false;
}

bool CJackSync::IsWarpSuppressed(playerid_t playerid)
{
    CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(playerid);
    CPed* ped = playerid == CNetworkPlayerManager::m_localPlayerId
                   ? FindPlayerPed(0) : (player != nullptr ? player->m_pPed : nullptr);
    if (IsPlayerJackInProgress(ped))
        return true;

    auto it = ms_warpSuppressedUntil.find(playerid);
    if (it == ms_warpSuppressedUntil.end())
        return false;

    if (static_cast<int32_t>(GetTickCount() - it->second) >= 0)
    {
        ms_warpSuppressedUntil.erase(it);
        return false;
    }

    return true;
}

void CJackSync::SuppressPedWarp(pedid_t pedid, uint32_t durationMs)
{
    if (!pedid.valid())
        return;

    ms_pedWarpSuppressedUntil[pedid] = GetTickCount() + durationMs;
}

bool CJackSync::IsPedWarpSuppressed(pedid_t pedid)
{
    auto it = ms_pedWarpSuppressedUntil.find(pedid);
    if (it == ms_pedWarpSuppressedUntil.end())
        return false;

    if (static_cast<int32_t>(GetTickCount() - it->second) >= 0)
    {
        ms_pedWarpSuppressedUntil.erase(it);
        return false;
    }

    return true;
}

bool CJackSync::IsPedJackInProgress(pedid_t pedid)
{
    // its syncer already scheduled the exit, or we are still inside the animation window
    if (ms_pendingPedJacks.find(pedid) != ms_pendingPedJacks.end())
        return true;

    return IsPedWarpSuppressed(pedid);
}

void CJackSync::NotifyRemoteJacked(CNetworkPlayer* victim, vehicleid_t vehicleid, int seatid, int door)
{
    if (!CNetwork::m_bAuthenticated || victim == nullptr || !vehicleid.valid())
        return;

    // stop the victim's own sync from putting the copy back in the seat during the drag
    SuppressWarp(victim->m_playerId);

    Packets::Vehicles::PlayerJacked packet{};
    packet.victimPlayerId = victim->m_playerId;
    packet.vehicleid = vehicleid;
    packet.seatid = static_cast<int8_t>(std::clamp(seatid, -1, 7));
    packet.door = static_cast<int8_t>(SanitizeDoor(door));
    GetPacketFactory().Send(packet);

    if (CConfigLoader::ms_bPlayerJackNative)
    {
        // Same reliable EVENT channel: release the victim before claiming the thief's seat.
        Packets::Vehicles::VehicleEnter enter{};
        enter.vehicleid = vehicleid;
        enter.bPassenger = seatid >= 0;
        enter.seatid = seatid >= 0 ? seatid : 0;
        GetPacketFactory().Send(enter);
    }
}

void CJackSync::NotifyPedJacked(CNetworkPed* ped, vehicleid_t vehicleid, int seatid, int door)
{
    if (!CNetwork::m_bAuthenticated || ped == nullptr || !ped->m_pedId.valid() || !vehicleid.valid())
        return;

    // the copy locally dragged out must not be warped back while its syncer reacts
    SuppressPedWarp(ped->m_pedId);

    // our own game is dragging it out right now (the enter car task): keep it seated until it is out
    if (ms_pendingPedJacks.find(ped->m_pedId) == ms_pendingPedJacks.end())
    {
        SPedJack pending{};
        pending.startedAt = GetTickCount();
        pending.vehicleid = vehicleid;
        pending.thiefPlayerId = CNetworkPlayerManager::m_localPlayerId;
        pending.bReacted = true;  // the enter car task gave it the event already
        ms_pendingPedJacks[ped->m_pedId] = pending;
    }

    Packets::Peds::PedJacked packet{};
    packet.pedid = ped->m_pedId;
    packet.vehicleid = vehicleid;
    packet.seatid = static_cast<int8_t>(std::clamp(seatid, -1, 7));
    packet.door = static_cast<int8_t>(SanitizeDoor(door));
    // the syncer gives the ped an explicit reaction (flee) towards the player that pulled it out
    packet.thiefPlayerId = CNetworkPlayerManager::m_localPlayerId;
    GetPacketFactory().Send(packet);
}

void CJackSync::ApplyPedJacked(pedid_t pedid, vehicleid_t vehicleid, int seatid, int door, playerid_t thiefPlayerId)
{
    CNetworkPed* networkPed = CNetworkPedManager::GetPed(pedid);
    if (networkPed == nullptr || networkPed->m_pPed == nullptr)
        return;

    CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(vehicleid);
    CVehicle* vehicle = networkVehicle != nullptr ? networkVehicle->m_pVehicle : nullptr;

    CPed* ped = networkPed->m_pPed;
    if (vehicle == nullptr || !vehicle->IsVTableValid() || !ped->m_nPedFlags.bInVehicle || ped->m_pVehicle != vehicle)
        return;

    // repeated packets must not restart the animation or postpone the exit
    auto it = ms_pendingPedJacks.find(pedid);
    if (it != ms_pendingPedJacks.end())
        return;

    // C2: the game's own dragged out task everywhere (the same animation on every screen); on its syncer also the
    // event, so it flees / fights like in the normal game. The copies stay seated (IsPedJackInProgress) until out.
    SPedJack pending{};
    pending.startedAt = GetTickCount();
    pending.vehicleid = vehicleid;
    pending.thiefPlayerId = thiefPlayerId;
    pending.bReacted = StartNativeDraggedOut(networkPed, vehicle, seatid, door, thiefPlayerId);
    ms_pendingPedJacks[pedid] = pending;

    CMissionMirror::LogEventf("ped-jacked rx syncer=%d seat=%d door=%d event=%d", networkPed->m_bSyncing ? 1 : 0, seatid,
        door, pending.bReacted ? 1 : 0);
}

void CJackSync::ApplyLocalJacked(vehicleid_t vehicleid, int seatid, int door)
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(vehicleid);

    if (localPlayer == nullptr || networkVehicle == nullptr || networkVehicle->m_pVehicle == nullptr)
        return;

    CVehicle* vehicle = networkVehicle->m_pVehicle;
    if (!localPlayer->m_nPedFlags.bInVehicle || localPlayer->m_pVehicle != vehicle)
        return;

    // a repeated packet must not restart the animation or postpone the exit
    if (ms_localJack.pending)
        return;

    if (door < 0 || door > 11)
        door = CCarEnterExit::ComputeTargetDoorToExit(vehicle, localPlayer);

    door = SanitizeDoor(door);

    const bool bNative =
        CConfigLoader::ms_bPlayerJackNative && StartNativeDraggedOutPlayer(localPlayer, vehicle, seatid, door);
    if (bNative)
    {
        // everybody was already told by the jacker: our own "being jacked" notice is not needed
        ms_bSentBeingJacked = true;
        SuppressWarp(CNetworkPlayerManager::m_localPlayerId, NATIVE_JACK_TIMEOUT_MS);
        CMissionMirror::LogEventf("player-jacked native seat=%d door=%d", seatid, door);
    }
    else
    {
        PlayJackedTask(localPlayer, vehicle, seatid, door);
    }

    ms_localJack.pending = true;
    ms_localJack.native = bNative;
    ms_localJack.leaveAt = GetTickCount() + (bNative ? NATIVE_JACK_TIMEOUT_MS : LOCAL_JACK_ANIM_MS);
    ms_localJack.vehicleid = vehicleid;
    ms_localJack.seatid = seatid;
    ms_localJack.door = door;
}

bool CJackSync::ReplayThief(playerid_t thiefId, playerid_t victimId, vehicleid_t vehicleId, int seatid, int door)
{
    if (!CConfigLoader::ms_bPlayerJackNative || thiefId == CNetworkPlayerManager::m_localPlayerId ||
        thiefId == victimId || seatid < -1 || seatid >= 8)
        return false;
    auto thief = GetJackPlayerPed(thiefId);
    auto victim = GetJackPlayerPed(victimId);
    auto networkVehicle = CNetworkVehicleManager::GetVehicle(vehicleId);
    auto vehicle = networkVehicle != nullptr ? networkVehicle->m_pVehicle : nullptr;
    if (thief == nullptr || victim == nullptr || vehicle == nullptr || !thief->IsVTableValid() ||
        !victim->IsVTableValid() || !vehicle->IsVTableValid() || !thief->IsAlive() || !victim->IsAlive() ||
        thief->m_pIntelligence == nullptr || victim->m_pIntelligence == nullptr ||
        thief->m_nPedFlags.bInVehicle || !victim->m_nPedFlags.bInVehicle || victim->m_pVehicle != vehicle ||
        thief->m_nAreaCode != victim->m_nAreaCode || thief->m_nAreaCode != vehicle->m_nAreaCode ||
        (thief->GetPosition() - vehicle->GetPosition()).Magnitude() > 12.0f ||
        (seatid == -1 ? vehicle->m_pDriver : vehicle->m_apPassengers[seatid]) != victim)
        return false;

    // gta-reversed CarEnterExit.cpp:172/432 selects the victim by door, including bikes.
    // EnterCar.cpp:801 creates BOTH native tasks. Do not start the victim ahead of the thief.
    door = SanitizeDoor(door);
    if (door == 0)
        door = 10;  // SDK's front-left sentinel is native door 10.
    // The SDK declares this pointer-returning function as int.
    if (reinterpret_cast<CPed*>(CCarEnterExit::ComputeSlowJackedPed(vehicle, door)) != victim)
        return false;
    auto existing = ms_thiefReplays.find(thiefId);
    if (existing != ms_thiefReplays.end() && existing->second.victim == victimId &&
        existing->second.vehicle == vehicleId && GetReplayTask(thiefId, existing->second) != nullptr)
        return true;
    CTaskComplexEnterCar* task;
    if (seatid < 0)
    {
        task = new CTaskComplexEnterCarAsDriver(vehicle);
        task->m_nTargetDoor = door;
    }
    else
    {
        task = new CTaskComplexEnterCarAsPassenger(vehicle, door, false);
    }
    victim->m_nPedFlags.CantBeKnockedOffBike = 2;
    thief->m_pIntelligence->m_TaskMgr.SetTask(task, TASK_PRIMARY_PRIMARY, false);
    ms_thiefReplays[thiefId] = {victimId, vehicleId, GetTickCount(), seatid, door};
    CMissionMirror::LogEventf("player-jacked thief-replay seat=%d door=%d", seatid, door);
    return true;
}

void CJackSync::PlayRemoteJacked(CPlayerPed* ped, CVehicle* vehicle, int seatid, int door)
{
    if (ped == nullptr || vehicle == nullptr || IsPlayerJackInProgress(ped))
        return;

    // do not move it: only the animation, its own sync keeps sending the position
    if (!ped->m_nPedFlags.bInVehicle || ped->m_pVehicle != vehicle)
        return;

    // player-jack-native: the copy of the victim goes through the same task here (its warp is suppressed by the
    // caller, and its own on foot sync places it once it is out)
    if (CConfigLoader::ms_bPlayerJackNative && StartNativeDraggedOutPlayer(ped, vehicle, seatid, door))
        return;

    PlayJackedTask(ped, vehicle, seatid, door);
}

void CJackSync::Process()
{
    const uint32_t now = GetTickCount();

    for (auto it = ms_thiefReplays.begin(); it != ms_thiefReplays.end();)
    {
        auto task = GetReplayTask(it->first, it->second);
        if (!IsReplayValid(it->first, it->second) || now - it->second.startedAt >= 8000)
        {
            const auto thiefId = it->first;
            const auto replay = it->second;
            // Never leave a cancelled replay fighting the next authoritative transform.
            if (task != nullptr)
                GetJackPlayerPed(it->first)->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);
            it = ms_thiefReplays.erase(it);
            if (CNetwork::m_bAuthenticated && CConfigLoader::ms_bPlayerJackNative &&
                replay.victim == CNetworkPlayerManager::m_localPlayerId)
            {
                auto thief = GetJackPlayerPed(thiefId);
                auto victim = FindPlayerPed(0);
                if (thief != nullptr && thief->IsVTableValid() && thief->IsAlive() &&
                    victim != nullptr && victim->IsVTableValid() && victim->IsAlive() &&
                    thief->m_nAreaCode == victim->m_nAreaCode && !IsPedBeingDraggedOut(victim))
                    ApplyLocalJacked(replay.vehicle, replay.seatid, replay.door);
            }
        }
        else
            ++it;
    }

    for (auto it = ms_warpSuppressedUntil.begin(); it != ms_warpSuppressedUntil.end();)
    {
        if (static_cast<int32_t>(now - it->second) >= 0)
            it = ms_warpSuppressedUntil.erase(it);
        else
            ++it;
    }

    for (auto it = ms_pedWarpSuppressedUntil.begin(); it != ms_pedWarpSuppressedUntil.end();)
    {
        if (static_cast<int32_t>(now - it->second) >= 0)
            it = ms_pedWarpSuppressedUntil.erase(it);
        else
            ++it;
    }

    // C3: the jacked animation is over: let the streaming free the STDCARAMIMS block again
    for (auto it = ms_jackedAnimBlockReleaseAt.begin(); it != ms_jackedAnimBlockReleaseAt.end();)
    {
        if (static_cast<int32_t>(now - *it) >= 0)
        {
            if (ms_nJackedAnimBlock > 0)
                CAnimManager::RemoveAnimBlockRef(ms_nJackedAnimBlock);
            it = ms_jackedAnimBlockReleaseAt.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // an NPC being dragged out by the game's task: done once it is out and standing (or stuck: warp it out)
    for (auto it = ms_pendingPedJacks.begin(); it != ms_pendingPedJacks.end();)
    {
        CNetworkPed* networkPed = CNetworkPedManager::GetPed(it->first);
        if (networkPed == nullptr || networkPed->m_pPed == nullptr)
        {
            it = ms_pendingPedJacks.erase(it);
            continue;
        }

        CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(it->second.vehicleid);
        CVehicle* vehicle = networkVehicle != nullptr ? networkVehicle->m_pVehicle : nullptr;

        CPed* ped = networkPed->m_pPed;
        bool inCar = vehicle != nullptr && ped->m_nPedFlags.bInVehicle && ped->m_pVehicle == vehicle;
        bool timedOut = static_cast<int32_t>(now - (it->second.startedAt + PED_JACK_TIMEOUT_MS)) >= 0;

        if ((inCar || IsPedBeingDraggedOut(ped)) && !timedOut)
        {
            ++it;
            continue;
        }

        // the dragged out task got stuck: warp like before
        if (inCar)
        {
            CMissionMirror::LogEventf("ped-jacked timeout: warp out");
            networkPed->RemoveFromVehicle(vehicle, true);
        }

        // no event could be given (thief not here): explicit flee
        if (networkPed->m_bSyncing && !it->second.bReacted)
            ApplyJackReaction(networkPed, it->second.thiefPlayerId, vehicle);

        it = ms_pendingPedJacks.erase(it);
    }

    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (localPlayer == nullptr || !localPlayer->IsVTableValid() || localPlayer->m_pIntelligence == nullptr)
        return;

    CTaskManager& taskManager = localPlayer->m_pIntelligence->m_TaskMgr;

    // remember the seat: the dragged out task can start after the game already cleared the vehicle pointer
    if (localPlayer->m_nPedFlags.bInVehicle && localPlayer->m_pVehicle != nullptr)
    {
        if (CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(localPlayer->m_pVehicle))
        {
            ms_lastVehicleId = networkVehicle->m_vehicleId;
            ms_lastVehicleAt = now;
            ms_lastSeatId = -1;

            for (int i = 0; i < 8; i++)
            {
                if (localPlayer->m_pVehicle->m_apPassengers[i] == localPlayer)
                {
                    ms_lastSeatId = i;
                    break;
                }
            }

            ms_lastDoor =
                SanitizeDoor(CCarEnterExit::ComputeTargetDoorToExit(localPlayer->m_pVehicle, localPlayer));
        }
    }

    CTask* primaryTask = taskManager.m_aPrimaryTasks[TASK_PRIMARY_PRIMARY];
    int taskType = primaryTask != nullptr ? primaryTask->GetTaskType() : -1;
    bool beingJacked = IsBeingDraggedOutTask(taskType);

    if (!beingJacked && primaryTask != nullptr)
    {
        // the dragged out state may be a child of a complex task
        static const int draggedOutTasks[] = {TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT,
            TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT_AND_STAND_UP, TASK_COMPLEX_CAR_QUICK_BE_DRAGGED_OUT,
            TASK_SIMPLE_CAR_QUICK_BE_DRAGGED_OUT, TASK_SIMPLE_CAR_SET_PED_QUICK_DRAGGED_OUT,
            TASK_SIMPLE_CAR_SLOW_BE_DRAGGED_OUT, TASK_SIMPLE_CAR_SET_PED_SLOW_DRAGGED_OUT, TASK_SIMPLE_BIKE_JACKED};

        for (int draggedOutTask : draggedOutTasks)
        {
            if (taskManager.FindActiveTaskByType(draggedOutTask) != nullptr)
            {
                beingJacked = true;
                break;
            }
        }
    }

    // the local player is being pulled out by any ped (an NPC, or another player's ped on its own screen)
    if (beingJacked && !ms_bSentBeingJacked && !ms_localJack.pending && ms_lastVehicleId.valid() &&
        static_cast<uint32_t>(now - ms_lastVehicleAt) < 2000)
    {
        Packets::Vehicles::PlayerBeingJacked packet{};
        packet.vehicleid = ms_lastVehicleId;
        packet.seatid = static_cast<int8_t>(ms_lastSeatId);
        packet.door = static_cast<int8_t>(ms_lastDoor);
        GetPacketFactory().Send(packet);

        ms_bSentBeingJacked = true;
    }
    else if (!beingJacked && !ms_localJack.pending)
    {
        ms_bSentBeingJacked = false;
    }

    // entering as a passenger: the enter car hook only sees the driver task, this covers the passenger seat
    CTask* jackTask = primaryTask != nullptr && primaryTask->GetTaskType() == TASK_COMPLEX_ENTER_CAR_AS_PASSENGER
                          ? primaryTask
                          : nullptr;

    if (!CConfigLoader::ms_bPlayerJackNative && jackTask != ms_lastPassengerJackTask)
    {
        ms_lastPassengerJackTask = jackTask;

        if (jackTask != nullptr)
        {
            CTaskComplexEnterCar* enterTask = reinterpret_cast<CTaskComplexEnterCar*>(jackTask);
            CVehicle* vehicle = enterTask->m_pTargetVehicle;

            if (vehicle != nullptr && vehicle->IsVTableValid())
            {
                int seatid = CCarEnterExit::ComputePassengerIndexFromCarDoor(vehicle, enterTask->m_nTargetDoor);

                if (seatid >= 0 && seatid < 8)
                {
                    CPed* occupant = vehicle->m_apPassengers[seatid];
                    CNetworkPlayer* victim = CNetworkPlayerManager::GetPlayer(occupant);
                    CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle);

                    if (victim != nullptr && networkVehicle != nullptr)
                        CJackSync::NotifyRemoteJacked(
                            victim, networkVehicle->m_vehicleId, seatid, enterTask->m_nTargetDoor);
                }
            }
        }
    }

    // C5: an NPC is told it was jacked when our enter car task really starts dragging it out (CreateDragPedOutSubTask
    // gave it the dragged out task), not when the player only started walking to the door
    CTask* enterTask = primaryTask != nullptr && (primaryTask->GetTaskType() == TASK_COMPLEX_ENTER_CAR_AS_DRIVER ||
                                                     primaryTask->GetTaskType() == TASK_COMPLEX_ENTER_CAR_AS_PASSENGER)
                           ? primaryTask
                           : nullptr;
    CPed* draggedPed = enterTask != nullptr ? reinterpret_cast<CTaskComplexEnterCar*>(enterTask)->m_pDraggedPed : nullptr;

    if (draggedPed == nullptr)
    {
        ms_pLastDraggedPed = nullptr;
    }
    else if (draggedPed != ms_pLastDraggedPed && IsPedBeingDraggedOut(draggedPed))
    {
        ms_pLastDraggedPed = draggedPed;

        CTaskComplexEnterCar* enterCar = reinterpret_cast<CTaskComplexEnterCar*>(enterTask);
        CVehicle* vehicle = enterCar->m_pTargetVehicle;
        CNetworkPed* victimPed = CNetworkPedManager::GetPed(draggedPed);
        CNetworkVehicle* networkVehicle = vehicle != nullptr ? CNetworkVehicleManager::GetVehicle(vehicle) : nullptr;

        CNetworkPlayer* victimPlayer = CNetworkPlayerManager::GetPlayer(draggedPed);
        if (CConfigLoader::ms_bPlayerJackNative && victimPlayer != nullptr && networkVehicle != nullptr)
        {
            int seatid = enterCar->m_bAsDriver ? -1 :
                CCarEnterExit::ComputePassengerIndexFromCarDoor(vehicle, enterCar->m_nTargetDoor);
            if ((enterCar->m_bAsDriver || seatid >= 0) && seatid < 8)
            {
                draggedPed->m_nPedFlags.CantBeKnockedOffBike = 2;
                NotifyRemoteJacked(victimPlayer, networkVehicle->m_vehicleId, seatid, enterCar->m_nTargetDoor);
                CMissionMirror::LogEventf("player-jacked drag-start seat=%d door=%d", seatid, enterCar->m_nTargetDoor);
            }
        }

        if (victimPed != nullptr && networkVehicle != nullptr)
        {
            int seatid = -1;
            for (int i = 0; i < 8; i++)
            {
                if (vehicle->m_apPassengers[i] == draggedPed)
                {
                    seatid = i;
                    break;
                }
            }

            CJackSync::NotifyPedJacked(victimPed, networkVehicle->m_vehicleId, seatid, enterCar->m_nTargetDoor);
            CMissionMirror::LogEventf("ped-jacked tx seat=%d door=%d", seatid, enterCar->m_nTargetDoor);
        }
    }

    // player-jack-native: the game's task took the local victim out by itself; tell everyone once it is standing
    if (ms_localJack.pending && ms_localJack.native && !localPlayer->m_nPedFlags.bInVehicle &&
        !IsPedBeingDraggedOut(localPlayer))
    {
        ms_localJack.pending = false;
        ms_localJack.native = false;

        Packets::Vehicles::VehicleExit exitPacket{};
        exitPacket.bForce = true;
        GetPacketFactory().Send(exitPacket);
    }

    // the animation is over: place the local victim outside (its on foot sync then removes it everywhere)
    if (ms_localJack.pending && static_cast<int32_t>(now - ms_localJack.leaveAt) >= 0)
    {
        ms_localJack.pending = false;
        ms_localJack.native = false;

        CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(ms_localJack.vehicleid);
        if (networkVehicle != nullptr && networkVehicle->m_pVehicle != nullptr &&
            localPlayer->m_nPedFlags.bInVehicle && localPlayer->m_pVehicle == networkVehicle->m_pVehicle)
        {
            CVehicle* vehicle = networkVehicle->m_pVehicle;

            localPlayer->m_nPedFlags.CantBeKnockedOffBike = 2;  // 2 - normal

            auto task = CTaskSimpleCarSetPedOut(vehicle, ms_localJack.door, false);
            task.ProcessPed(localPlayer);
            CUtil::ReleaseStackCarTask(task);

            Packets::Vehicles::VehicleExit exitPacket{};
            exitPacket.bForce = true;
            GetPacketFactory().Send(exitPacket);
        }
    }
}
