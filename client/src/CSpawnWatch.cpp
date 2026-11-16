#include "stdafx.h"
#include "CSpawnWatch.h"
#include "CNetworkPed.h"
#include "CNetworkVehicle.h"
#include "CStreamingSync.h"
#include "CMissionMirror.h"

// someone else's ambulance / fire truck this close to the accident or the fire is already answering it
static constexpr float EMERGENCY_ALREADY_THERE_RADIUS = 150.0f;
static constexpr uint32_t WATCH_INTERVAL_MS = 5000;
static constexpr float WATCH_RADIUS = 100.0f;
static constexpr int MAX_WATCH_LOGS = 150;

// CCarCtrl::GenerateOneEmergencyServicesCar (0x42B7D0: model, point of the accident / fire) called from
// CCarCtrl::GenerateEmergencyServicesCar (0x42F9C0, call sites 0x42FAE6 and 0x42FB89; gta-reversed CarCtrl.cpp:441).
// Every game sees the same accident or fire and sent its own vehicle: two fire trucks, two ambulances. The point
// belongs to one player (CStreamingSync::IsPointMine, the same split as the traffic), and nobody sends a second one
// while the vehicle of another player is already there.
static CAutomobile* __cdecl CCarCtrl__GenerateOneEmergencyServicesCar_Hook(unsigned int modelId, CVector posn)
{
    if (CNetwork::m_bAuthenticated && CConfigLoader::ms_bEmergencyDedupe)
    {
        bool bSkip = !CStreamingSync::IsPointMine(posn);

        for (CNetworkVehicle* networkVehicle : CNetworkVehicleManager::m_pVehicles)
        {
            if (bSkip)
                break;
            if (networkVehicle == nullptr || networkVehicle->m_bSyncing)
                continue;

            CVehicle* vehicle = networkVehicle->m_pVehicle;
            if (vehicle != nullptr && vehicle->IsVTableValid() && vehicle->m_nModelIndex == (int)modelId &&
                DistanceBetweenPoints(vehicle->GetPosition(), posn) < EMERGENCY_ALREADY_THERE_RADIUS)
                bSkip = true;
        }

        if (bSkip)
        {
            static uint32_t lastLog = 0;
            const uint32_t now = GetTickCount();
            if (!lastLog || now - lastLog >= 5000)
            {
                lastLog = now;
                CMissionMirror::LogEventf("emergency-skip model=%d", (int)modelId);
            }
            return nullptr;
        }
    }

    return plugin::CallAndReturn<CAutomobile*, 0x42B7D0, unsigned int, CVector>(modelId, posn);
}

void CSpawnWatch::InjectHooks()
{
    patch::RedirectCall(0x42FAE6, CCarCtrl__GenerateOneEmergencyServicesCar_Hook);
    patch::RedirectCall(0x42FB89, CCarCtrl__GenerateOneEmergencyServicesCar_Hook);
}

// 1 = this game syncs it, 0 = copy of another player, -1 = not on the network
static int GetPedSyncState(CPed* ped)
{
    CNetworkPed* networkPed = CNetworkPedManager::GetPed(ped);
    return networkPed == nullptr ? -1 : (networkPed->m_bSyncing ? 1 : 0);
}

static int GetVehicleSyncState(CVehicle* vehicle)
{
    CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle);
    return networkVehicle == nullptr ? -1 : (networkVehicle->m_bSyncing ? 1 : 0);
}

static bool IsEmergencyVehicle(CVehicle* vehicle)
{
    const int model = vehicle->m_nModelIndex;
    return vehicle->IsLawEnforcementVehicle() || model == 416 /* ambulance */ || model == 407 /* fire truck */ ||
           model == 544 /* fire truck with ladder */;
}

void CSpawnWatch::Process()
{
    if (!CNetwork::m_bAuthenticated || !CConfigLoader::ms_bSpawnWatch)
        return;

    static uint32_t lastRun = 0;
    static int nLogged = 0;
    const uint32_t now = GetTickCount();
    if (nLogged >= MAX_WATCH_LOGS || (lastRun != 0 && now - lastRun < WATCH_INTERVAL_MS))
        return;
    lastRun = now;

    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (localPlayer == nullptr || CPools::ms_pPedPool == nullptr || CPools::ms_pVehiclePool == nullptr)
        return;

    const CVector localPos = localPlayer->GetPosition();

    std::vector<CPed*> peds;
    int copsMine = 0, copsCopies = 0, copsLocalOnly = 0;
    for (CPed* ped : CPools::ms_pPedPool)
    {
        if (ped == nullptr || !ped->IsVTableValid() || ped->m_matrix == nullptr || ped->IsPlayer() || !ped->IsAlive() ||
            DistanceBetweenPoints(ped->GetPosition(), localPos) > WATCH_RADIUS)
            continue;

        peds.push_back(ped);
        if (ped->m_nPedType == PED_TYPE_COP)
        {
            const int state = GetPedSyncState(ped);
            (state == 1 ? copsMine : state == 0 ? copsCopies : copsLocalOnly)++;
        }
    }

    // two peds of the same model standing on the same spot
    int nPass = 0;
    for (size_t i = 0; i < peds.size() && nPass < 3; i++)
    {
        for (size_t j = i + 1; j < peds.size() && nPass < 3; j++)
        {
            if (peds[i]->m_nModelIndex != peds[j]->m_nModelIndex ||
                peds[i]->m_nPedFlags.bInVehicle != peds[j]->m_nPedFlags.bInVehicle)
                continue;

            const float distance = DistanceBetweenPoints(peds[i]->GetPosition(), peds[j]->GetPosition());
            if (!(distance <= 1.2f))  // also skips a NaN position (bad_positions of the debug export)
                continue;

            // a pair synced by the same side is the game's own grouping (gang groups, two cops of one car), not a
            // duplicate between games: only a pair whose sides differ (or two peds off the network) is logged
            const int stateI = GetPedSyncState(peds[i]), stateJ = GetPedSyncState(peds[j]);
            if (stateI == stateJ && stateI != -1)
                continue;

            nPass++;
            nLogged++;
            CMissionMirror::LogEventf("dup-ped model=%d type=%d createdBy=%d/%d sync=%d/%d inCar=%d dist=%.2f visible=%d/%d area=%u/%u",
                peds[i]->m_nModelIndex, (int)peds[i]->m_nPedType, (int)peds[i]->m_nCreatedBy, (int)peds[j]->m_nCreatedBy,
                GetPedSyncState(peds[i]), GetPedSyncState(peds[j]), (int)peds[i]->m_nPedFlags.bInVehicle, distance,
                (int)peds[i]->m_bIsVisible, (int)peds[j]->m_bIsVisible, peds[i]->m_nAreaCode, peds[j]->m_nAreaCode);
        }
    }

    // the cops around, by who syncs them (every 30 s while there are some)
    static uint32_t lastCops = 0;
    if (copsMine + copsCopies + copsLocalOnly > 0 && (lastCops == 0 || now - lastCops >= 30000))
    {
        lastCops = now;
        nLogged++;
        CWanted* wanted = FindPlayerWanted(0);
        CMissionMirror::LogEventf("cop-watch mine=%d copies=%d local=%d wanted=%d", copsMine, copsCopies, copsLocalOnly,
            wanted != nullptr ? (int)wanted->m_nWantedLevel : -1);
    }

    // two emergency vehicles of the same model close to each other, one ours and one of another player
    std::vector<CVehicle*> vehicles;
    for (CVehicle* vehicle : CPools::ms_pVehiclePool)
    {
        if (vehicle != nullptr && vehicle->IsVTableValid() && vehicle->m_matrix != nullptr && IsEmergencyVehicle(vehicle) &&
            DistanceBetweenPoints(vehicle->GetPosition(), localPos) < WATCH_RADIUS * 2.0f)
            vehicles.push_back(vehicle);
    }

    nPass = 0;
    for (size_t i = 0; i < vehicles.size() && nPass < 2; i++)
    {
        for (size_t j = i + 1; j < vehicles.size() && nPass < 2; j++)
        {
            const int stateA = GetVehicleSyncState(vehicles[i]);
            const int stateB = GetVehicleSyncState(vehicles[j]);
            if (vehicles[i]->m_nModelIndex != vehicles[j]->m_nModelIndex || stateA == stateB)
                continue;

            const float distance = DistanceBetweenPoints(vehicles[i]->GetPosition(), vehicles[j]->GetPosition());
            if (distance > 60.0f)
                continue;

            nPass++;
            nLogged++;
            CMissionMirror::LogEventf("dup-vehicle model=%d createdBy=%d/%d sync=%d/%d dist=%.1f",
                vehicles[i]->m_nModelIndex, (int)vehicles[i]->m_nCreatedBy, (int)vehicles[j]->m_nCreatedBy, stateA, stateB,
                distance);
        }
    }
}
