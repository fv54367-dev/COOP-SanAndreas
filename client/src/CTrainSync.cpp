#include "stdafx.h"
#include "CTrainSync.h"
#include "CNetwork.h"
#include "CNetworkVehicle.h"
#include "CNetworkVehicleManager.h"
#include "CNetworkPlayer.h"
#include "CNetworkPlayerManager.h"
#include "CNetworkPedManager.h"
#include "CLocalPlayer.h"
#include "CConfigLoader.h"
#include "CMissionMirror.h"
#include <vector>
#include <CTrain.h>
#include <CPopulation.h>
#include <cmath>
#include <cstdarg>

using namespace Packets::Trains;

// gta_sa.exe 1.0 US, checked against gta-reversed (Train.cpp: pTrackNodes 0xC38024, NumTrackNodes 0xC38014,
// arrTotalTrackLength 0xC37FEC; Train.h: GenTrain_* 0xC38000+) and the design read from the bytes
// (_refs project_gta_sa_coop docs/design/trains.md):
// - sixteen train configurations at 0x8D44F8, 64 bytes each, model ids terminated by 0
// - CTrain::DoTrainGenerationAndRemoval 0x6F7900, called at 0x43426D inside CCarCtrl::GenerateRandomCars
// - CTrain::ProcessControl 0x6F86A0 (its audio call at 0x6F86B6 is NOPed in CPatch::FixCrashes)
namespace
{
constexpr int MAX_TRAINS = 32;
constexpr int MAX_CARRIAGES = Packets::Trains::STrainSpawnDesc::MAX_CARRIAGES;
constexpr float GENERATOR_KEEP_RADIUS = 220.0f;  // the generator deletes non-mission trains at 220 m from its camera
constexpr float SNAP_DISTANCE = 25.0f;           // display error above this snaps to the state
constexpr float JUMP_DISTANCE = 5.0f;            // state off its own prediction more than this = teleport
constexpr uint32_t STATE_HOLD_MS = 500;          // no fresh state for this long: the copy holds
constexpr uint32_t REBUILD_COOLDOWN_MS = 3000;   // after a local loss, wait before rebuilding (own DELETE_ALL_TRAINS)

struct STrainMirror
{
    Packets::Trains::STrainSpawnDesc desc{};
    CTrain* pHead = nullptr;      // follower: the bound copy engine (null = lost, rebuild at the next state)
    bool bBound = false;          // follower: the copy exists
    uint32_t nLostAt = 0;         // follower: when the copy was lost locally (rebuild cooldown)
    // host bookkeeping
    uint8_t jumpCounter = 0;
    uint8_t hostSeq = 0;
    int hostHeadRef = -1;
    float fLastSentDistance = 0.0f;
    float fLastSentSpeed = 0.0f;
    uint32_t nLastPosLog = 0;     // tick of the last position line in the log (evidence for the Catalyst check)
    uint32_t nLastSentAt = 0;    // tick of the last state (train-sync-rail: the jump test uses the real elapsed time)
    // follower: applied host state
    float fHostDistance = 0.0f;
    float fHostSpeed = 0.0f;      // m per 1/50 s step, signed along the track
    uint8_t fGas = 0;             // applied gas/brake of the host's engine (the copy's audio reads them)
    uint8_t fBrake = 0;
    uint8_t hostFlags = 0;
    uint8_t lastSeq = 0;
    uint8_t appliedJump = 0;      // follower: last applied jump counter
    bool bHasState = false;
    bool bHasSequence = false;
    uint32_t nStateTime = 0;
    float fDisplayDistance = 0.0f;  // dead-reckoned rail distance the copy is pinned to
};

STrainMirror ms_trains[MAX_TRAINS];
bool ms_used[MAX_TRAINS] = {};
uint8_t ms_nextTrainId = 0;

// a TRAIN_STATE (SYNC channel) can arrive before its TRAIN_EVENT SPAWN (EVENT channel): keep the latest per id
Packets::Trains::TrainState ms_pending[MAX_TRAINS];
bool ms_pendingUsed[MAX_TRAINS] = {};

void TrainLog(const char* format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    logger::info("%s", text);
    CMissionMirror::LogEventf("%s", text);
}

// Pool handles include the allocation generation: never follow a reused address.
struct SCreatedCopy
{
    uint8_t trainid;
    int headRef;
    std::vector<int> carriages;
};
std::vector<SCreatedCopy> ms_copies;

CTrain* LiveTrain(int ref)
{
    if (ref < 0)
        return nullptr;
    CVehicle* vehicle = CPools::GetVehicle(ref);
    return vehicle && vehicle->m_nVehicleSubType == VEHICLE_TRAIN ? (CTrain*)vehicle : nullptr;
}

bool IsCreatedCopy(CTrain* carriage)
{
    for (const auto& copy : ms_copies)
        for (int ref : copy.carriages)
            if (LiveTrain(ref) == carriage)
                return true;
    return false;
}

void DeleteCreatedCopy(const SCreatedCopy& copy, const char* reason)
{
    // Walk the live chain first; remembered handles also cover a deleted head or detached tail.
    std::vector<int> chain;
    CTrain* carriage = LiveTrain(copy.headRef);
    while (carriage && IsCreatedCopy(carriage))
    {
        int ref = CPools::GetVehicleRef(carriage);
        if (std::find(chain.begin(), chain.end(), ref) != chain.end())
            break;
        chain.push_back(ref);
        CTrain* next = carriage->m_pNextCarriage;
        carriage = nullptr;
        for (int saved : copy.carriages)
            if (LiveTrain(saved) == next)
                carriage = LiveTrain(saved);
    }
    for (int ref : copy.carriages)
        if (std::find(chain.begin(), chain.end(), ref) == chain.end())
            chain.push_back(ref);

    bool logged = false;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        CTrain* vehicle = LiveTrain(*it);
        if (!vehicle)
            continue;
        if (!logged)
        {
            CVector pos = vehicle->GetPosition();
            TrainLog("train-orphan-removed id=%d pos=%.1f,%.1f,%.1f reason=%s",
                copy.trainid, pos.x, pos.y, pos.z, reason);
            logged = true;
        }
        // The engine's cosmetic driver belongs to this creation too.
        CTrainSync::EvictCosmeticDriver(vehicle);
        CNetworkVehicle* record = CNetworkVehicleManager::GetVehicle(vehicle);
        if (record)
            record->DestroyGameVehicle();
        else
        {
            auto warpOut = [vehicle](CPed* ped)
            {
                if (ped && ped->IsVTableValid() && ped->m_nPedFlags.bInVehicle)
                {
                    CVector pos = vehicle->GetPosition();
                    plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(
                        CPools::GetPedRef(ped), pos.x + 2.0f, pos.y, pos.z + 0.5f);
                }
            };
            warpOut(vehicle->m_pDriver);
            for (int seat = 0; seat < 8 && seat < vehicle->m_nMaxPassengers; seat++)
                warpOut(vehicle->m_apPassengers[seat]);
            CWorld::RemoveReferencesToDeletedObject(vehicle);
            CWorld::Remove(vehicle);
            delete vehicle;
        }
    }
}

void SweepOrphanCopies(bool all = false)
{
    for (size_t i = 0; i < ms_copies.size();)
    {
        const auto& copy = ms_copies[i];
        const auto& mirror = ms_trains[copy.trainid];
        if (!all && ms_used[copy.trainid] && mirror.bBound &&
            LiveTrain(copy.headRef) && mirror.pHead == LiveTrain(copy.headRef))
        {
            i++;
            continue;
        }
        DeleteCreatedCopy(copy, all ? "reset-role-disconnect" : "unbound");
        ms_copies.erase(ms_copies.begin() + i);
    }
}

float TrackLength(uint8_t track)
{
    float* lengths = (float*)0xC37FEC;  // arrTotalTrackLength[4]
    return track < 4 ? lengths[track] : 0.0f;
}

float WrapDistance(float d, float length)
{
    if (length <= 0.0f)
        return d;
    d = fmodf(d, length);
    if (d < 0.0f)
        d += length;
    return d;
}

// shortest difference modulo the track length
float WrapError(float target, float current, float length)
{
    if (length <= 0.0f)
        return target - current;
    float err = fmodf(target - current, length);
    if (err > length * 0.5f)
        err -= length;
    if (err < -length * 0.5f)
        err += length;
    return err;
}

bool SeqNewer(uint8_t candidate, uint8_t last)
{
    return candidate != last && (uint8_t)(candidate - last) < 128;
}

bool IsKnownOccupant(CPed* pPed)
{
    if (pPed == nullptr)
        return false;
    if (pPed->IsPlayer())
        return true;
    return CNetworkPlayerManager::GetPlayer((CEntity*)pPed) != nullptr ||
        CNetworkPedManager::GetPed((CEntity*)pPed) != nullptr;
}

STrainMirror* FindById(uint8_t trainid)
{
    return trainid < MAX_TRAINS && ms_used[trainid] ? &ms_trains[trainid] : nullptr;
}

// the head vehicle of a bound follower copy
STrainMirror* FindByHeadVehicle(CVehicle* pVehicle)
{
    for (int id = 0; id < MAX_TRAINS; id++)
    {
        if (!ms_used[id] || !ms_trains[id].bBound)
            continue;

        CNetworkVehicle* pRecord = CNetworkVehicleManager::GetVehicle(ms_trains[id].desc.carriages[0].vehicleid);
        if (pRecord != nullptr && pRecord->m_pVehicle == pVehicle && ms_trains[id].pHead == pVehicle)
            return &ms_trains[id];
    }
    return nullptr;
}

STrainMirror* FindByCarriageId(vehicleid_t vehicleid)
{
    if (!vehicleid.valid())
        return nullptr;

    for (int id = 0; id < MAX_TRAINS; id++)
    {
        if (!ms_used[id])
            continue;

        for (int i = 0; i < MAX_CARRIAGES; i++)
        {
            if (ms_trains[id].desc.carriages[i].vehicleid == vehicleid)
                return &ms_trains[id];
        }
    }
    return nullptr;
}

bool SameDescriptor(const Packets::Trains::STrainSpawnDesc& a, const Packets::Trains::STrainSpawnDesc& b)
{
    if (a.track != b.track || a.config != b.config || a.count != b.count ||
        (a.flags & TRAINFLAG_CLOCKWISE) != (b.flags & TRAINFLAG_CLOCKWISE))
        return false;

    for (int i = 0; i < a.count && i < MAX_CARRIAGES; i++)
    {
        if (a.carriages[i].vehicleid != b.carriages[i].vehicleid || a.carriages[i].modelid != b.carriages[i].modelid)
            return false;
    }
    return true;
}

// Configuration rows contain sixteen uint32 model ids, terminated by zero.
int GetConfigModels(int config, uint16_t* out)
{
    const uint32_t* row = (const uint32_t*)0x8D44F8 + config * 16;
    int count = 0;
    while (count < MAX_CARRIAGES && row[count] != 0)
    {
        out[count] = (uint16_t)row[count];
        count++;
    }
    return count;
}

bool ValidDescriptor(const Packets::Trains::STrainSpawnDesc& desc)
{
    if (desc.trainid >= MAX_TRAINS || desc.track >= 4 || desc.config >= 16 ||
        desc.count == 0 || desc.count > MAX_CARRIAGES || !std::isfinite(desc.railDistance))
        return false;
    const int* nodes = (const int*)0xC38014;
    const void* const* tracks = (const void* const*)0xC38024;
    float length = TrackLength(desc.track);
    if (!tracks[desc.track] || nodes[desc.track] <= 0 || desc.node >= nodes[desc.track] ||
        !std::isfinite(length) || length <= 0.0f || desc.railDistance < 0.0f || desc.railDistance >= length)
        return false;
    uint16_t models[MAX_CARRIAGES];
    if (GetConfigModels(desc.config, models) != desc.count)
        return false;
    for (int i = 0; i < desc.count; i++)
    {
        if (!desc.carriages[i].vehicleid.valid() || !IsTrainModel(desc.carriages[i].modelid) ||
            desc.carriages[i].modelid != models[i])
            return false;
        for (int j = 0; j < i; j++)
            if (desc.carriages[i].vehicleid == desc.carriages[j].vehicleid)
                return false;
    }
    return true;
}

// the chain must have been built from a vanilla configuration for the copy to be exact (CreateMissionTrain only
// builds whole configurations); odd remnants (a Car.Delete on an engine) stay unsynced
int MatchConfig(int count, const uint16_t* models)
{
    for (int config = 0; config < 16; config++)
    {
        uint16_t configModels[MAX_CARRIAGES];
        int configCount = GetConfigModels(config, configModels);
        if (configCount != count)
            continue;

        bool same = true;
        for (int i = 0; i < count; i++)
        {
            if (configModels[i] != models[i])
            {
                same = false;
                break;
            }
        }
        if (same)
            return config;
    }
    return -1;
}

bool BuildDescriptor(CTrain* pHead, Packets::Trains::STrainSpawnDesc& desc)
{
    uint16_t models[MAX_CARRIAGES];
    int count = 0;
    CTrain* pCarriage = pHead;
    while (pCarriage != nullptr && count < MAX_CARRIAGES)
    {
        models[count++] = (uint16_t)pCarriage->m_nModelIndex;
        pCarriage = pCarriage->m_pNextCarriage;
    }

    int config = MatchConfig(count, models);
    if (config < 0 || pHead->m_nNodeIndex < 0 || pHead->m_nTrackId < 0)
        return false;

    desc.track = (uint8_t)pHead->m_nTrackId;
    desc.config = (uint8_t)config;
    desc.flags = pHead->m_nTrainFlags.bClockwiseDirection ? TRAINFLAG_CLOCKWISE : 0;
    desc.node = (uint16_t)pHead->m_nNodeIndex;
    desc.count = (uint8_t)count;
    desc.railDistance = pHead->m_fCurrentRailDistance;
    desc.speed = (int16_t)std::clamp((int)(pHead->m_fTrainSpeed * 10000.0f), -10000, 10000);
    desc.jumpCounter = 0;
    desc.gas = (uint8_t)std::clamp(pHead->m_fTrainGas, 0.0f, 255.0f);
    desc.brake = (uint8_t)std::clamp(pHead->m_fTrainBrake, 0.0f, 255.0f);

    pCarriage = pHead;
    for (int i = 0; i < count && pCarriage != nullptr; i++)
    {
        desc.carriages[i].vehicleid = vehicleid_t();  // filled by the caller from the records
        desc.carriages[i].modelid = models[i];
        desc.carriages[i].lock = (uint8_t)pCarriage->m_eDoorLock;
        pCarriage = pCarriage->m_pNextCarriage;
    }
    return true;
}

// refresh the state part of the descriptor from the live head (used for snapshots)
void UpdateDescState(STrainMirror* pTrain)
{
    CNetworkVehicle* pHeadRecord = CNetworkVehicleManager::GetVehicle(pTrain->desc.carriages[0].vehicleid);
    CTrain* pHead = pHeadRecord && pHeadRecord->m_pVehicle ? (CTrain*)pHeadRecord->m_pVehicle : nullptr;
    if (pHead == nullptr)
        return;

    pTrain->desc.railDistance = pHead->m_fCurrentRailDistance;
    pTrain->desc.speed = (int16_t)std::clamp((int)(pHead->m_fTrainSpeed * 10000.0f), -10000, 10000);
    pTrain->desc.jumpCounter = pTrain->jumpCounter;
    pTrain->desc.gas = (uint8_t)std::clamp(pHead->m_fTrainGas, 0.0f, 255.0f);
    pTrain->desc.brake = (uint8_t)std::clamp(pHead->m_fTrainBrake, 0.0f, 255.0f);
    pTrain->desc.flags = (pTrain->desc.flags & TRAINFLAG_CLOCKWISE) |
        (pHead->m_nTrainFlags.bNotOnARailRoad ? TRAINFLAG_DERAILED : 0) |
        ((pHead->m_pDriver != nullptr && !IsKnownOccupant(pHead->m_pDriver)) ? TRAINFLAG_ENGINE_HAS_NPC_DRIVER : 0) |
        (pHead->m_nTrainFlags.bStoppedAtStation ? TRAINFLAG_STOPPED_AT_STATION : 0);
}

// host: send the head's state (the jump counter goes up on a teleport or a direction change so the copy snaps)
void SendState(int id, STrainMirror* pTrain)
{
    CNetworkVehicle* pHeadRecord = CNetworkVehicleManager::GetVehicle(pTrain->desc.carriages[0].vehicleid);
    CTrain* pHead = pHeadRecord && pHeadRecord->m_pVehicle ? (CTrain*)pHeadRecord->m_pVehicle : nullptr;
    if (pHead == nullptr)
        return;

    float distance = pHead->m_fCurrentRailDistance;
    float speed = std::clamp(pHead->m_fTrainSpeed, -1.0f, 1.0f);
    float length = TrackLength(pTrain->desc.track);
    // 200 ms = 10 steps; with train-sync-rail the real elapsed time (a late tick no longer reads as a teleport)
    uint32_t nowTick = GetTickCount();
    float steps = 10.0f;
    if (CConfigLoader::ms_bTrainSyncRail && pTrain->nLastSentAt != 0)
    {
        steps = std::clamp((nowTick - pTrain->nLastSentAt) / 20.0f, 1.0f, 50.0f);
        // The copies extrapolate the rail distance, so they need the speed the rail distance really moves at, not the
        // engine's: a script-driven train (Catalyst: Car.StartPlayback of a recorded path) keeps m_fTrainSpeed at 0, or
        // with the opposite sign of its distance change (log 2026-11-12: speed -0.4 with the distance going up), and the
        // copy, never corrected against the sent direction, trailed 12-25 m and snapped forward every 25 m
        float moved = WrapError(distance, pTrain->fLastSentDistance, length) / steps;
        if (fabsf(moved) <= 1.0f)
            speed = moved;
    }
    float expected = WrapDistance(pTrain->fLastSentDistance + pTrain->fLastSentSpeed * steps, length);
    float err = WrapError(distance, expected, length);

    bool bJump = fabsf(err) > JUMP_DISTANCE;
    bool bDirChanged = pTrain->fLastSentSpeed != 0.0f && speed != 0.0f &&
        (pTrain->fLastSentSpeed > 0.0f) != (speed > 0.0f);
    if (bJump || bDirChanged)
        pTrain->jumpCounter++;

    Packets::Trains::TrainState packet{};
    packet.trainid = (uint8_t)id;
    packet.seq = pTrain->hostSeq++;
    packet.track = pTrain->desc.track;
    packet.flags = (pTrain->desc.flags & TRAINFLAG_CLOCKWISE) |
        (pHead->m_nTrainFlags.bNotOnARailRoad ? TRAINFLAG_DERAILED : 0) |
        ((pHead->m_pDriver != nullptr && !IsKnownOccupant(pHead->m_pDriver)) ? TRAINFLAG_ENGINE_HAS_NPC_DRIVER : 0) |
        (pHead->m_nTrainFlags.bStoppedAtStation ? TRAINFLAG_STOPPED_AT_STATION : 0);
    packet.jumpCounter = pTrain->jumpCounter;
    packet.railDistance = distance;
    packet.speed = (int16_t)std::clamp((int)(speed * 10000.0f), -10000, 10000);
    packet.gas = (uint8_t)std::clamp(pHead->m_fTrainGas, 0.0f, 255.0f);
    packet.brake = (uint8_t)std::clamp(pHead->m_fTrainBrake, 0.0f, 255.0f);
    GetPacketFactory().Send(packet);

    pTrain->fLastSentDistance = distance;
    pTrain->fLastSentSpeed = speed;
    pTrain->nLastSentAt = nowTick;

    if (CConfigLoader::ms_bTrainSyncRail && nowTick - pTrain->nLastPosLog >= 5000 && pHead->m_matrix)
    {
        pTrain->nLastPosLog = nowTick;
        CVector pos = pHead->GetPosition();
        TrainLog("train-sync: host pos id=%d dist=%.1f speed=%.3f (engine %.3f) at=%.1f,%.1f,%.1f", id, distance, speed,
            pHead->m_fTrainSpeed, pos.x, pos.y, pos.z);
    }
}

// host: register a chain head the scan found. Returns the new train id or -1
int RegisterTrain(CTrain* pHead)
{
    Packets::Trains::STrainSpawnDesc desc;
    if (!BuildDescriptor(pHead, desc))
    {
        static uint32_t lastLog = 0;
        if (GetTickCount() - lastLog >= REBUILD_COOLDOWN_MS)
        {
            lastLog = GetTickCount();
            TrainLog("train-sync: chain with no matching configuration, left local (head model %d)",
                pHead->m_nModelIndex);
        }
        return -1;
    }

    int id = -1;
    for (int i = 0; i < MAX_TRAINS; i++)
    {
        int candidate = (ms_nextTrainId + i) % MAX_TRAINS;
        if (!ms_used[candidate])
        {
            id = candidate;
            break;
        }
    }
    if (id < 0)
        return -1;
    ms_nextTrainId = (uint8_t)((id + 1) % MAX_TRAINS);

    STrainMirror* pTrain = &ms_trains[id];
    *pTrain = STrainMirror{};
    ms_used[id] = true;
    pTrain->desc = desc;
    pTrain->hostHeadRef = CPools::GetVehicleRef(pHead);
    pTrain->desc.trainid = (uint8_t)id;

    CTrain* pCarriage = pHead;
    for (int i = 0; i < desc.count && pCarriage != nullptr; i++)
    {
        CNetworkVehicle* pRecord = CNetworkVehicleManager::GetVehicle(pCarriage);
        if (pRecord == nullptr)
            pRecord = CNetworkVehicle::CreateHosted(pCarriage);
        if (pRecord == nullptr)
        {
            TrainLog("train-sync: no record for carriage %d, train not synced", i);
            ms_used[id] = false;
            return -1;
        }
        pTrain->desc.carriages[i].vehicleid = pRecord->m_vehicleId;
        pCarriage = pCarriage->m_pNextCarriage;
    }

    Packets::Trains::TrainEvent packet{};
    packet.kind = Packets::Trains::TrainEvent::KIND_SPAWN;
    packet.count = 1;
    packet.trains[0] = pTrain->desc;
    GetPacketFactory().Send(packet);

    pTrain->fLastSentDistance = desc.railDistance;
    pTrain->fLastSentSpeed = desc.speed / 10000.0f;
    SendState(id, pTrain);
    TrainLog("train-sync: host registered train %d (config %d, %d carriages)", id, desc.config, desc.count);
    return id;
}

// follower: build the copy with the game's own CreateMissionTrain and bind the carriages to their records
bool CreateCopy(STrainMirror* pTrain)
{
    Packets::Trains::STrainSpawnDesc& desc = pTrain->desc;
    pTrain->nLostAt = GetTickCount();  // failed builds retry at most once per cooldown

    for (int i = 0; i < desc.count; i++)
    {
        int modelid = desc.carriages[i].modelid;
        unsigned char oldFlags = CStreaming::ms_aInfoForModel[modelid].m_nFlags;
        CStreaming::RequestModel(modelid, GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
        if (CStreaming::ms_aInfoForModel[modelid].m_nLoadState != LOADSTATE_LOADED)
        {
            TrainLog("train-sync: model %d not loaded, copy not built", modelid);
            if (!(oldFlags & GAME_REQUIRED))
                CStreaming::SetModelIsDeletable(modelid);
            return false;
        }
        if (!(oldFlags & GAME_REQUIRED))
        {
            CStreaming::SetModelIsDeletable(modelid);
            CStreaming::SetModelTxdIsDeletable(modelid);
        }
    }

    CTrain* pFirst = nullptr;
    CTrain* pLast = nullptr;
    CTrain::CreateMissionTrain(CVector(0.0f, 0.0f, 0.0f), (desc.flags & TRAINFLAG_CLOCKWISE) != 0, desc.config,
        &pFirst, &pLast, desc.node, desc.track, false);

    if (pFirst == nullptr)
        return false;

    SCreatedCopy copy{ desc.trainid, CPools::GetVehicleRef(pFirst), {} };
    for (CTrain* carriage = pFirst; carriage; carriage = carriage->m_pNextCarriage)
        copy.carriages.push_back(CPools::GetVehicleRef(carriage));
    ms_copies.push_back(copy);

    // the chain must match the descriptor carriage by carriage (models and count)
    CTrain* pCarriage = pFirst;
    for (int i = 0; i < desc.count; i++)
    {
        if (pCarriage == nullptr || pCarriage->m_nModelIndex != desc.carriages[i].modelid)
        {
            TrainLog("train-sync: copy chain mismatch (train %d), removed", desc.trainid);
            SweepOrphanCopies();
            return false;
        }
        pCarriage = pCarriage->m_pNextCarriage;
    }

    if (pCarriage != nullptr)
    {
        TrainLog("train-sync: copy %d has extra carriages", desc.trainid);
        SweepOrphanCopies();
        return false;
    }

    // bind each carriage to its record (same model, record without a live vehicle)
    CTrain* aCarriages[MAX_CARRIAGES] = {};
    pCarriage = pFirst;
    for (int i = 0; i < desc.count; i++)
    {
        CNetworkVehicle* pRecord = CNetworkVehicleManager::GetVehicle(desc.carriages[i].vehicleid);
        if (pRecord == nullptr || pRecord->m_pVehicle != nullptr || pRecord->m_nModelId != desc.carriages[i].modelid)
        {
            TrainLog("train-sync: record for carriage %d missing or taken, copy not built", i);
            SweepOrphanCopies();
            return false;
        }
        aCarriages[i] = pCarriage;
        pCarriage = pCarriage->m_pNextCarriage;
    }

    for (int i = 0; i < desc.count; i++)
    {
        CNetworkVehicle* pRecord = CNetworkVehicleManager::GetVehicle(desc.carriages[i].vehicleid);
        pRecord->m_pVehicle = aCarriages[i];
        aCarriages[i]->RegisterReference((CEntity**)&pRecord->m_pVehicle);
        pRecord->m_bStreamedOut = false;
        pRecord->m_nCreatedBy = aCarriages[i]->m_nCreatedBy;

        // the mission flag protects the copy from this game's removal paths and switches the streak coaches'
        // random passengers off; the copy never drives itself
        aCarriages[i]->m_nTrainFlags.bMissionTrain = true;
        aCarriages[i]->m_nTrainFlags.bStopsAtStations = false;
        aCarriages[i]->m_nTrainFlags.bForceSlowDown = false;
        aCarriages[i]->m_eDoorLock = (eDoorLock)desc.carriages[i].lock;
    }

    pTrain->pHead = pFirst;
    pTrain->bBound = true;
    pTrain->nLostAt = 0;

    // CreateMissionTrain gave the engine an NPC driver; the host's state says whether his train has one
    if (!(pTrain->hostFlags & TRAINFLAG_ENGINE_HAS_NPC_DRIVER))
        CTrainSync::EvictCosmeticDriver(pFirst);

    TrainLog("train-sync: copy %d built (config %d, %d carriages)", desc.trainid, desc.config, desc.count);
    return true;
}

// follower: remove every owned carriage independently of records; the descriptor stays until
// TRAIN_EVENT REMOVE forgets the whole entry
void RemoveCopy(STrainMirror* pTrain, bool forgetRecords = false)
{
    for (size_t i = 0; i < ms_copies.size();)
    {
        if (ms_copies[i].trainid != pTrain->desc.trainid)
        {
            i++;
            continue;
        }
        DeleteCreatedCopy(ms_copies[i], "remove-copy");
        ms_copies.erase(ms_copies.begin() + i);
    }
    if (forgetRecords)
    {
        for (int i = 0; i < pTrain->desc.count; i++)
        {
            CNetworkVehicle* record = CNetworkVehicleManager::GetVehicle(pTrain->desc.carriages[i].vehicleid);
            // Host simulation records are not owned by the mirror.
            if (!record || record->m_bSyncing || record->m_pVehicle != nullptr)
                continue;
            CNetworkVehicleManager::Remove(record);
            delete record;
        }
    }

    pTrain->pHead = nullptr;
    pTrain->bBound = false;
}

void FreeAll()
{
    for (int id = 0; id < MAX_TRAINS; id++)
    {
        if (ms_used[id])
            RemoveCopy(&ms_trains[id], true);
        ms_used[id] = false;
        ms_pendingUsed[id] = false;
    }
    SweepOrphanCopies(true);
}

// host: DoTrainGenerationAndRemoval only knows this game's own camera. In one call it deletes every chain farther
// than 220 m from it (0x6F7AC3) and, earlier, remembers ANY front carriage without the mission flag as "a train is
// out there" (0x6F7A43) to skip generation entirely (0x6F7D6D). So the mission flag may only cover a chain the
// generator would delete on its own while a network player still needs it: masking a chain that sits near the host's
// own camera removes it from that count and the generator spawns another one every few seconds (log 19-11: nine
// trains, four per direction, frozen over the LS line). The cap bounds the worst case: once an unmasked train
// exists the generator stops spawning by itself.
CTrain* ms_aKeptHeads[8];
int ms_nKeptCount = 0;
constexpr int MAX_KEPT_TRAINS = 2;  // one player rides one train
constexpr float KEEP_RESERVE_DISTANCE = GENERATOR_KEEP_RADIUS + 60.0f;  // the camera never sits this far off the ped

void KeepTrainsNearNetworkPlayers()
{
    ms_nKeptCount = 0;
    if (!CConfigLoader::ms_bTrainSyncKeep || CNetworkPlayerManager::m_pPlayers.empty())
        return;

    CPed* pLocalPed = FindPlayerPed(0);
    if (pLocalPed == nullptr || pLocalPed->m_matrix == nullptr)
        return;
    const CVector localPos = pLocalPed->GetPosition();

    for (auto vehicle : CPools::ms_pVehiclePool)
    {
        if (!vehicle || vehicle->m_nVehicleSubType != VEHICLE_TRAIN)
            continue;

        CTrain* pHead = (CTrain*)vehicle;
        if (pHead->m_pPrevCarriage != nullptr || pHead->m_nTrainFlags.bMissionTrain)
            continue;

        // the generator would keep this chain by itself: leave it visible to its own bookkeeping
        bool bOutOfHostView = true;
        for (CTrain* pCarriage = pHead; pCarriage != nullptr && bOutOfHostView; pCarriage = pCarriage->m_pNextCarriage)
            if (DistanceBetweenPoints(pCarriage->GetPosition(), localPos) < KEEP_RESERVE_DISTANCE)
                bOutOfHostView = false;
        if (!bOutOfHostView)
            continue;

        // ...and only rescue it while a network player still has it on screen
        bool bNeeded = false;
        for (CTrain* pCarriage = pHead; pCarriage != nullptr && !bNeeded; pCarriage = pCarriage->m_pNextCarriage)
        {
            CVector pos = pCarriage->GetPosition();
            for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
            {
                if (pNetworkPlayer->m_pPed == nullptr || pNetworkPlayer->m_pPed->m_matrix == nullptr)
                    continue;
                if (DistanceBetweenPoints(pos, pNetworkPlayer->m_pPed->GetPosition()) < GENERATOR_KEEP_RADIUS)
                {
                    bNeeded = true;
                    break;
                }
            }
        }
        if (!bNeeded || ms_nKeptCount >= MAX_KEPT_TRAINS)
            continue;

        for (CTrain* pCarriage = pHead; pCarriage != nullptr; pCarriage = pCarriage->m_pNextCarriage)
            pCarriage->m_nTrainFlags.bMissionTrain = true;
        ms_aKeptHeads[ms_nKeptCount++] = pHead;
    }

    // Evidence for the next in-game test: one line when the generator starts or stops being blind to a chain.
    static CTrain* s_aLastKept[MAX_TRAINS] = {};
    static int s_nLastKept = 0;
    static uint32_t s_nLastKeepLog = 0;
    bool bChanged = s_nLastKept != ms_nKeptCount;
    for (int i = 0; !bChanged && i < ms_nKeptCount; i++)
        bChanged = s_aLastKept[i] != ms_aKeptHeads[i];
    uint32_t now = GetTickCount();
    if (bChanged && now - s_nLastKeepLog >= 5000)
    {
        s_nLastKept = ms_nKeptCount;
        for (int i = 0; i < ms_nKeptCount; i++)
            s_aLastKept[i] = ms_aKeptHeads[i];
        s_nLastKeepLog = now;
        TrainLog("train-sync: keep masked=%d (chains hidden from the generator)", ms_nKeptCount);
    }
}

void RestoreKeptTrains()
{
    for (int i = 0; i < ms_nKeptCount; i++)
    {
        CTrain* pHead = ms_aKeptHeads[i];
        for (CTrain* pCarriage = pHead; pCarriage != nullptr; pCarriage = pCarriage->m_pNextCarriage)
            pCarriage->m_nTrainFlags.bMissionTrain = false;
    }
    ms_nKeptCount = 0;
}

// host: is any carriage of this chain already part of a registered train?
bool IsAnyCarriageRegistered(CTrain* pHead)
{
    if (IsCreatedCopy(pHead))
        return true;
    for (int id = 0; id < MAX_TRAINS; id++)
        if (ms_used[id] && LiveTrain(ms_trains[id].hostHeadRef) == pHead)
            return true;
    for (CTrain* pCarriage = pHead; pCarriage != nullptr; pCarriage = pCarriage->m_pNextCarriage)
    {
        CNetworkVehicle* pRecord = CNetworkVehicleManager::GetVehicle(pCarriage);
        if (pRecord != nullptr && FindByCarriageId(pRecord->m_vehicleId) != nullptr)
            return true;
    }
    return false;
}

// host: evidence that the generator keeps the population at vanilla level. It only ever spawns while no unmasked
// train exists (0x6F7D6D), so a head count that keeps growing means something is hiding chains from it again.
void LogTrainPopulation(int heads)
{
    static int s_nLastHeads = -1;
    static uint32_t s_nLastLog = 0;

    uint32_t now = GetTickCount();
    bool bAnomaly = heads >= 3;
    if (heads == s_nLastHeads && (!bAnomaly || now - s_nLastLog < 10000))
        return;

    int registered = 0;
    for (int id = 0; id < MAX_TRAINS; id++)
        if (ms_used[id])
            registered++;

    s_nLastHeads = heads;
    s_nLastLog = now;
    TrainLog("train-sync: population heads=%d registered=%d", heads, registered);
}

// host: find new chains, forget gone ones, send the state of every registered train
void HostProcess()
{
    int nHeads = 0;
    for (auto vehicle : CPools::ms_pVehiclePool)
    {
        if (!vehicle || vehicle->m_nVehicleSubType != VEHICLE_TRAIN)
            continue;

        CTrain* pHead = (CTrain*)vehicle;
        if (pHead->m_pPrevCarriage != nullptr)
            continue;

        nHeads++;
        if (!IsAnyCarriageRegistered(pHead))
            RegisterTrain(pHead);
    }
    LogTrainPopulation(nHeads);

    for (int id = 0; id < MAX_TRAINS; id++)
    {
        if (!ms_used[id])
            continue;

        STrainMirror* pTrain = &ms_trains[id];
        CNetworkVehicle* pHeadRecord = CNetworkVehicleManager::GetVehicle(pTrain->desc.carriages[0].vehicleid);
        if (pHeadRecord == nullptr || pHeadRecord->m_pVehicle == nullptr ||
            !pHeadRecord->m_pVehicle->IsVTableValid())
        {
            // the host's train is gone: the per-carriage VEHICLE_REMOVEs went out through the CWorld::Remove
            // redirects; the copy chains learn it from this event
            Packets::Trains::TrainEvent packet{};
            packet.kind = Packets::Trains::TrainEvent::KIND_REMOVE;
            packet.trainid = (uint8_t)id;
            GetPacketFactory().Send(packet);

            TrainLog("train-sync: host train %d gone, REMOVE sent", id);
            ms_used[id] = false;
            continue;
        }

        SendState(id, pTrain);
    }
}

// DisableRandomTrains only changes a flag; existing random chains need explicit removal.
void RemoveLocalRandomTrains()
{
    // delete the random trains this game generated while it was on its own (or before joining): they would
    // duplicate the host's. Mission trains of a running local mission stay (T-D6)
    std::vector<CTrain*> heads;
    for (auto vehicle : CPools::ms_pVehiclePool)
    {
        if (!vehicle || vehicle->m_nVehicleSubType != VEHICLE_TRAIN)
            continue;

        CTrain* pHead = (CTrain*)vehicle;
        if (pHead->m_pPrevCarriage != nullptr || pHead->m_nTrainFlags.bMissionTrain ||
            FindByHeadVehicle(pHead) != nullptr)
            continue;
        heads.push_back(pHead);
    }

    for (CTrain* pHead : heads)
    {
        if (!pHead->IsVTableValid())
            continue;

        // Diagnose once per removed chain, including randoms left before the role arrived.
        CVector pos = pHead->GetPosition();
        CMissionMirror::LogEventf("train-local-random head=%p model=%d track=%d pos=%.1f,%.1f,%.1f",
            pHead, pHead->m_nModelIndex, pHead->m_nTrackId, pos.x, pos.y, pos.z);
        std::vector<CTrain*> chain;
        for (CTrain* carriage = pHead; carriage != nullptr; carriage = carriage->m_pNextCarriage)
            chain.push_back(carriage);

        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            CTrain* pCarriage = *it;
            CNetworkVehicle* pRecord = CNetworkVehicleManager::GetVehicle(pCarriage);
            auto warpOut = [pCarriage](CPed* ped)
            {
                if (!ped || !ped->IsVTableValid() || !ped->m_nPedFlags.bInVehicle)
                    return;
                CVector pos = pCarriage->GetPosition();
                plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(
                    CPools::GetPedRef(ped), pos.x + 2.0f, pos.y, pos.z + 0.5f);
            };
            // Same native exit path as CNetworkVehicle::DestroyGameVehicle.
            if (pCarriage->m_pDriver && !IsKnownOccupant(pCarriage->m_pDriver))
                CPopulation::RemovePed(pCarriage->m_pDriver);
            warpOut(pCarriage->m_pDriver);
            for (int seat = 0; seat < 8 && seat < pCarriage->m_nMaxPassengers; seat++)
                warpOut(pCarriage->m_apPassengers[seat]);
            CWorld::RemoveReferencesToDeletedObject(pCarriage);
            CWorld::Remove(pCarriage);
            delete pCarriage;

            if (pRecord != nullptr)
            {
                CNetworkVehicleManager::Remove(pRecord);
                delete pRecord;  // m_pVehicle is null already: no CleanUpOldReference, just the remove packet
            }
        }
    }
}

// follower: a chain this game destroyed locally (its own scripts, a restart) is rebuilt from the descriptor at
// the next state, after a cooldown so a local DELETE_ALL_TRAINS does not fight the rebuild
void FollowerWatchdog()
{
    uint32_t now = GetTickCount();
    for (int id = 0; id < MAX_TRAINS; id++)
    {
        if (!ms_used[id] || !ms_trains[id].bBound)
            continue;

        STrainMirror* pTrain = &ms_trains[id];
        for (int i = 0; i < pTrain->desc.count && i < MAX_CARRIAGES; i++)
        {
            CNetworkVehicle* pRecord = CNetworkVehicleManager::GetVehicle(pTrain->desc.carriages[i].vehicleid);
            CTrain* expected = nullptr;
            CTrain* expectedNext = nullptr;
            CTrain* expectedPrev = nullptr;
            for (const auto& copy : ms_copies)
                if (copy.trainid == id && i < (int)copy.carriages.size())
                {
                    expected = LiveTrain(copy.carriages[i]);
                    if (i + 1 < (int)copy.carriages.size())
                        expectedNext = LiveTrain(copy.carriages[i + 1]);
                    if (i > 0)
                        expectedPrev = LiveTrain(copy.carriages[i - 1]);
                }
            if (!expected || !pRecord || pRecord->m_pVehicle != expected ||
                expected->m_pNextCarriage != expectedNext || expected->m_pPrevCarriage != expectedPrev)
            {
                TrainLog("train-sync: copy %d lost locally, waiting to rebuild", id);
                RemoveCopy(pTrain);
                pTrain->nLostAt = now;
                break;
            }
        }
    }
}
}  // namespace

bool CTrainSync::IsEnabled()
{
    return CConfigLoader::ms_bTrainSync && CNetwork::m_bAuthenticated;
}

bool CTrainSync::IsSyncedCarriage(const CNetworkVehicle* pVehicle)
{
    // records of trains only exist while train-sync keeps them (the spawn handler drops them otherwise)
    return Packets::Trains::IsTrainModel(pVehicle->m_nModelId);
}

void CTrainSync::Process()
{
    if (!IsEnabled())
    {
        FreeAll();  // switch off or disconnected: nothing of ours must survive
        return;
    }

    if (CLocalPlayer::m_bIsHost)
        HostProcess();
    else
    {
        CTrain::DisableRandomTrains(true);
        RemoveLocalRandomTrains();
        FollowerWatchdog();
        SweepOrphanCopies();
    }
}

void CTrainSync::SendSnapshot()
{
    if (!IsEnabled() || !CLocalPlayer::m_bIsHost)
        return;

    Packets::Trains::TrainEvent packet{};
    packet.kind = Packets::Trains::TrainEvent::KIND_SNAPSHOT;
    for (int id = 0; id < MAX_TRAINS && packet.count < 8; id++)
    {
        if (!ms_used[id])
            continue;

        UpdateDescState(&ms_trains[id]);
        packet.trains[packet.count++] = ms_trains[id].desc;
    }
    if (packet.count == 0)
        return;

    GetPacketFactory().Send(packet);
}

void CTrainSync::OnBecameHost()
{
    // any copies (and ids) from a previous authority go; the scan starts fresh from this game's own trains
    FreeAll();
}

void CTrainSync::OnBecameFollower()
{
    FreeAll();
    if (!IsEnabled())
        return;

    CTrain::DisableRandomTrains(true);
    RemoveLocalRandomTrains();
}

void CTrainSync::HandleSpawn(Packets::Trains::TrainEvent* pPacket)
{
    if (!IsEnabled() || CLocalPlayer::m_bIsHost)
        return;

    for (int i = 0; i < pPacket->count && i < 8; i++)
    {
        Packets::Trains::STrainSpawnDesc& desc = pPacket->trains[i];
        if (!ValidDescriptor(desc))
            continue;

        STrainMirror* pTrain = &ms_trains[desc.trainid];
        if (ms_used[desc.trainid] && pTrain->bBound && SameDescriptor(pTrain->desc, desc))
        {
            // known copy, same train: nothing to rebuild (the state keeps it in place)
            continue;
        }

        if (ms_used[desc.trainid] && pTrain->bBound)
            RemoveCopy(pTrain);  // the descriptor changed: rebuild from the new one

        if (!ms_used[desc.trainid])
        {
            *pTrain = STrainMirror{};
            ms_used[desc.trainid] = true;
        }
        pTrain->desc = desc;
        pTrain->desc.trainid = desc.trainid;
        pTrain->hostFlags = desc.flags;
        pTrain->fHostDistance = desc.railDistance;
        pTrain->fHostSpeed = desc.speed / 10000.0f;
        pTrain->fGas = desc.gas;
        pTrain->fBrake = desc.brake;
        pTrain->fDisplayDistance = desc.railDistance;
        pTrain->bHasState = true;
        pTrain->bHasSequence = false;
        pTrain->appliedJump = desc.jumpCounter;
        pTrain->nStateTime = GetTickCount();

        // a state may already be cached (it arrived before this SPAWN)
        if (ms_pendingUsed[desc.trainid])
        {
            Packets::Trains::TrainState pending = ms_pending[desc.trainid];
            ms_pendingUsed[desc.trainid] = false;
            HandleState(&pending);
            if (pTrain->bBound)
                continue;
        }

        // derailed on the host (descriptor or cached state): no copy until the host removes the train
        if (pTrain->hostFlags & TRAINFLAG_DERAILED)
            continue;

        if (!CreateCopy(pTrain))
            pTrain->fDisplayDistance = pTrain->fHostDistance;  // rebuilt at the next state
    }
}

void CTrainSync::HandleRemove(Packets::Trains::TrainEvent* pPacket)
{
    if (CLocalPlayer::m_bIsHost)
        return;
    STrainMirror* pTrain = FindById(pPacket->trainid);
    if (pTrain == nullptr)
        return;

    RemoveCopy(pTrain, true);
    ms_used[pPacket->trainid] = false;
    ms_pendingUsed[pPacket->trainid] = false;
    TrainLog("train-sync: train %d removed by the host", pPacket->trainid);
}

void CTrainSync::HandleState(Packets::Trains::TrainState* pPacket)
{
    if (!IsEnabled() || CLocalPlayer::m_bIsHost || pPacket->trainid >= MAX_TRAINS ||
        pPacket->track >= 4 || !std::isfinite(pPacket->railDistance) ||
        !std::isfinite(TrackLength(pPacket->track)) || TrackLength(pPacket->track) <= 0.0f ||
        pPacket->railDistance < 0.0f || pPacket->railDistance >= TrackLength(pPacket->track))
        return;

    STrainMirror* pTrain = &ms_trains[pPacket->trainid];
    if (!ms_used[pPacket->trainid])
    {
        // cache until the descriptor arrives (SYNC and EVENT have no mutual order)
        if (!ms_pendingUsed[pPacket->trainid] || SeqNewer(pPacket->seq, ms_pending[pPacket->trainid].seq))
        {
            ms_pending[pPacket->trainid] = *pPacket;
            ms_pendingUsed[pPacket->trainid] = true;
        }
        return;
    }

    if (pPacket->track != pTrain->desc.track)
        return;

    if (pTrain->bHasSequence && !SeqNewer(pPacket->seq, pTrain->lastSeq))
        return;

    pTrain->fHostDistance = pPacket->railDistance;
    pTrain->fHostSpeed = pPacket->speed / 10000.0f;
    pTrain->fGas = pPacket->gas;
    pTrain->fBrake = pPacket->brake;
    pTrain->hostFlags = pPacket->flags;
    pTrain->lastSeq = pPacket->seq;
    pTrain->bHasSequence = true;
    pTrain->bHasState = true;
    pTrain->nStateTime = GetTickCount();

    // derailed on the host: the copy goes and stays gone until the bit clears (no rebuild loop)
    if (pPacket->flags & TRAINFLAG_DERAILED)
    {
        if (pTrain->bBound)
        {
            TrainLog("train-sync: train %d derailed on the host, copy removed", pPacket->trainid);
            RemoveCopy(pTrain);
        }
        pTrain->nLostAt = GetTickCount();
        return;
    }

    if (!pTrain->bBound)
    {
        // lost copy: rebuild from the kept descriptor (cooldown so a local DELETE_ALL_TRAINS doesn't loop)
        if (GetTickCount() - pTrain->nLostAt > REBUILD_COOLDOWN_MS && CreateCopy(pTrain))
            pTrain->fDisplayDistance = pTrain->fHostDistance;
        return;
    }

    // a jump (teleport, direction change) snaps the display straight to the state
    if (pPacket->jumpCounter != pTrain->appliedJump)
    {
        pTrain->appliedJump = pPacket->jumpCounter;
        pTrain->fDisplayDistance = pTrain->fHostDistance;
    }
}

bool CTrainSync::OverrideCopyProcessControl(CVehicle* pVehicle)
{
    if (!IsEnabled() || CLocalPlayer::m_bIsHost)
        return false;
    if (pVehicle->m_nVehicleSubType != VEHICLE_TRAIN)
        return false;

    STrainMirror* pTrain = FindByHeadVehicle(pVehicle);
    if (pTrain == nullptr || !pTrain->bBound || !pTrain->bHasState)
        return false;
    if (pTrain->hostFlags & TRAINFLAG_DERAILED)
        return false;

    CTrain* pTrainEngine = (CTrain*)pVehicle;
    float length = TrackLength(pTrain->desc.track);
    uint32_t now = GetTickCount();
    float dt = CTimer::ms_fTimeStep;  // in 1/50 s units

    bool bFreshState = now - pTrain->nStateTime <= STATE_HOLD_MS;
    if (bFreshState)
    {
        // + the state's trip here (half the ping, as the NPC copies): with 100 ms of ping the copy ran behind the host and
        // was pushed forward at every state, which a guest standing on a wagon felt as jolts (Catalyst, 2026-11-13)
        const float tripMs = std::min(CNetwork::GetRTT() * 0.5f, 150.0f);
        float stepsSinceState = (now - pTrain->nStateTime + tripMs) / 20.0f;  // ms -> 1/50 s steps
        float predicted = WrapDistance(pTrain->fHostDistance + pTrain->fHostSpeed * stepsSinceState, length);
        if (CConfigLoader::ms_bTrainSyncRail)
        {
            // dead reckon + correct in one move: a copy that ran ahead (host braked) is slowed down, down to a
            // stop, never pushed back, so the lead decays instead of staying forever; one that lags speeds up
            float advanced = WrapDistance(pTrain->fDisplayDistance + pTrain->fHostSpeed * dt, length);
            float railErr = WrapError(predicted, advanced, length);
            if (fabsf(railErr) > SNAP_DISTANCE)
                pTrain->fDisplayDistance = predicted;
            else
            {
                float move = pTrain->fHostSpeed * dt + railErr * std::clamp(dt * 0.02f / 0.5f, 0.0f, 1.0f);
                if (pTrain->fHostSpeed != 0.0f && (move > 0.0f) != (pTrain->fHostSpeed > 0.0f))
                    move = 0.0f;
                pTrain->fDisplayDistance = WrapDistance(pTrain->fDisplayDistance + move, length);
            }
        }
        else
        {
        // dead reckon from the last state
        pTrain->fDisplayDistance = WrapDistance(pTrain->fDisplayDistance + pTrain->fHostSpeed * dt, length);

        // correct toward the state's prediction, spread over ~0.5 s and never against the direction of motion
        float err = WrapError(predicted, pTrain->fDisplayDistance, length);
        if (fabsf(err) > SNAP_DISTANCE)
        {
            pTrain->fDisplayDistance = predicted;
        }
        else if (err != 0.0f)
        {
            float fraction = std::clamp(CTimer::ms_fTimeStep * 0.02f / 0.5f, 0.0f, 1.0f);
            if (pTrain->fHostSpeed == 0.0f || (err > 0.0f) == (pTrain->fHostSpeed > 0.0f))
                pTrain->fDisplayDistance = WrapDistance(pTrain->fDisplayDistance + err * fraction, length);
        }
        }
    }

    plugin::CallMethod<0x502280, CAEVehicleAudioEntity*>(&pTrainEngine->m_vehicleAudio);

    char savedCruise = pTrainEngine->m_autoPilot.m_nCruiseSpeed;
    unsigned char savedStatus = pTrainEngine->m_nStatus;

    // pin the engine for the original call: speed 0 and cruise 0 under status 4 integrate nothing (no pad, no
    // station logic, no derail check); the original still places every carriage from the rail distance
    pTrainEngine->m_fCurrentRailDistance = pTrain->fDisplayDistance;
    pTrainEngine->m_fTrainSpeed = 0.0f;
    pTrainEngine->m_autoPilot.m_nCruiseSpeed = 0;
    pTrainEngine->m_nStatus = 4;
    plugin::CallMethodDyn<CVehicle*>(0x6F86A0, pTrainEngine);
    pTrainEngine->m_nStatus = savedStatus;
    pTrainEngine->m_autoPilot.m_nCruiseSpeed = savedCruise;
    pTrainEngine->m_fTrainSpeed = pTrain->fHostSpeed;  // the carriages behind and scripts follow this
    pTrainEngine->m_fTrainGas = (float)pTrain->fGas;
    pTrainEngine->m_fTrainBrake = (float)pTrain->fBrake;

    if (CConfigLoader::ms_bTrainSyncRail && now - pTrain->nLastPosLog >= 5000 && pTrainEngine->m_matrix)
    {
        pTrain->nLastPosLog = now;
        CVector pos = pTrainEngine->GetPosition();
        TrainLog("train-sync: copy pos id=%d dist=%.1f host=%.1f speed=%.3f at=%.1f,%.1f,%.1f", pTrain->desc.trainid,
            pTrain->fDisplayDistance, pTrain->fHostDistance, pTrain->fHostSpeed, pos.x, pos.y, pos.z);
    }
    return true;
}

void CTrainSync::ProcessGeneration()
{
    // Block before authentication too: reconnects must not run the generator under a stale role.
    if (CConfigLoader::ms_bTrainSync && (!CNetwork::m_bAuthenticated || !CLocalPlayer::m_bIsHost))
    {
        CTrain::DisableRandomTrains(true);
        return;
    }

    bool bKeep = IsEnabled() && CLocalPlayer::m_bIsHost;
    if (bKeep)
        KeepTrainsNearNetworkPlayers();

    CTrain::DoTrainGenerationAndRemoval();

    if (bKeep)
        RestoreKeptTrains();
}

bool CTrainSync::OnCarriageRecordRemoved(CNetworkVehicle* pRecord)
{
    STrainMirror* pTrain = FindByCarriageId(pRecord->m_vehicleId);
    if (pTrain == nullptr || CLocalPlayer::m_bIsHost)
        return false;

    // the whole copy goes (the host removed its train or a carriage); TRAIN_EVENT REMOVE arrives right behind
    // on the same reliable channel, and until then the rebuild cooldown blocks a stale state from resurrecting it
    RemoveCopy(pTrain, true);
    ms_used[pTrain->desc.trainid] = false;
    ms_pendingUsed[pTrain->desc.trainid] = false;
    return true;
}

void CTrainSync::EvictCosmeticDriver(CVehicle* pVehicle)
{
    CPed* pDriver = pVehicle->m_pDriver;
    if (pDriver == nullptr || IsKnownOccupant(pDriver))
        return;

    CPopulation::RemovePed(pDriver);
}
