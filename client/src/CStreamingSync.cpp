#include "stdafx.h"
#include "CStreamingSync.h"
#include "network/packets/train.h"
#include "CNetworkVehicle.h"
#include "CNetworkPed.h"
#include <COpCodeSync.h>

static bool IsAircraftModel(int modelId)
{
    if (modelId < 400 || modelId > 611 || !CModelInfo::ms_modelInfoPtrs[modelId])
        return false;

    auto type = ((CVehicleModelInfo*)CModelInfo::ms_modelInfoPtrs[modelId])->m_nVehicleType;
    return type == VEHICLE_PLANE || type == VEHICLE_HELI;
}

bool CStreamingSync::IsAmbientVehicle(unsigned char createdBy)
{
    return createdBy == RANDOM_VEHICLE || createdBy == PARKED_VEHICLE;
}

bool CStreamingSync::IsAmbientPed(unsigned char createdBy)
{
    return createdBy != MISSION_CHAR;
}

// mission peds of a coop mission are shared: every client must always see them (never suppress them)
static bool IsSyncedMissionPed(unsigned char createdBy)
{
    return createdBy == MISSION_CHAR && COpCodeSync::IsSyncedMissionRunning();
}

// keep the area of the record updated: the game doesn't tag a ped created while we were outside the interior
static void LearnRemotePedArea(CNetworkPed* networkPed, CPed* ped, const CPlayerPed* localPlayer)
{
    if (ped->m_nAreaCode != 0)
    {
        networkPed->m_nAreaCode = ped->m_nAreaCode;
        return;
    }

    if (localPlayer->m_nAreaCode != 0 &&
        DistanceBetweenPoints(localPlayer->GetPosition(), ped->GetPosition()) < CStreamingSync::INTERIOR_SAME_AREA_RADIUS)
    {
        networkPed->m_nAreaCode = localPlayer->m_nAreaCode;
        if (CConfigLoader::ms_bInteriorPopulationSync)
            ped->m_nAreaCode = localPlayer->m_nAreaCode;
    }
}

unsigned char CStreamingSync::GetLocalInteriorArea()
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    return localPlayer ? localPlayer->m_nAreaCode : 0;
}

bool CStreamingSync::ShouldSuppressPedSpawn(unsigned char createdBy, const CVector& pos, unsigned char creatorArea)
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer || localPlayer->m_nAreaCode == 0)
        return false;

    // coop mission peds are shared by every client
    if (IsSyncedMissionPed(createdBy))
        return false;

    // only the owner of the interior hosts its peds: a spawn from a guest is the tie backup
    if (!CInteriorOwnership::IsOwner())
        return false;

    // PED_SPAWN carries no area: trust the sender's area, or fall back to distance (interiors are far from the map)
    return creatorArea != 0 ? creatorArea == localPlayer->m_nAreaCode
                            : DistanceBetweenPoints(localPlayer->GetPosition(), pos) <
                                  CInteriorOwnership::SAME_INTERIOR_RADIUS;
}

// creating a vehicle on top of another one makes them explode
static bool IsSpotTakenByVehicle(const CVector& pos)
{
    for (auto vehicle : CPools::ms_pVehiclePool)
    {
        if (vehicle && DistanceBetweenPoints(vehicle->GetPosition(), pos) < 4.0f)
            return true;
    }
    return false;
}

static float DistanceToLocalPlayer(const CVector& pos)
{
    // during loading / respawn there may be a frame without a local ped (FindPlayerCoors would crash)
    CPlayerPed* localPlayer = FindPlayerPed(0);
    return localPlayer ? DistanceBetweenPoints(localPlayer->GetPosition(), pos) : -1.0f;
}

bool CStreamingSync::IsOtherPlayerGeneratingNearby()
{
    if (!CNetwork::m_bAuthenticated)
        return false;

    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (localPlayer == nullptr)
        return true;  // nothing to generate for: the other clients keep generating

    CVector localPos = localPlayer->GetPosition();

    for (CNetworkPlayer* networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!networkPlayer->m_pPed || networkPlayer->m_playerId.value() > CNetworkPlayerManager::m_localPlayerId.value())
            continue;

        if (DistanceBetweenPoints(localPos, networkPlayer->m_pPed->GetPosition()) < SHARED_GENERATION_RADIUS)
            return true;
    }

    return false;
}

bool CStreamingSync::IsPointMine(const CVector& p)
{
    CPlayerPed* localPlayer = FindPlayerPed(0);

    // interiors are handled by CInteriorOwnership: their peds/vehicles are not split by distance
    if (localPlayer == nullptr || localPlayer->m_nAreaCode != 0)
        return true;

    if (!CNetwork::m_bAuthenticated)
        return true;

    CVector2D localPos = localPlayer->GetPosition();
    float localDistance = DistanceBetweenPoints(CVector2D(p), localPos);
    uint32_t localId = CNetworkPlayerManager::m_localPlayerId.value();

    for (CNetworkPlayer* networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPlayerPed* ped = networkPlayer->m_pPed;
        if (ped == nullptr || ped == localPlayer || !ped->IsVTableValid() || ped->m_matrix == nullptr)
            continue;

        // only the players on the main map (our area, since we are outside) generate the same entities
        if (ped->m_nAreaCode != localPlayer->m_nAreaCode)
            continue;

        float distance = DistanceBetweenPoints(CVector2D(p), CVector2D(ped->GetPosition()));

        // strictly farther than us: his. Within the tie margin the player with the lowest id owns the point
        bool otherWins = networkPlayer->m_playerId.value() < localId
            ? distance < localDistance + POINT_OWN_TIE_MARGIN
            : distance < localDistance - POINT_OWN_TIE_MARGIN;

        if (otherWins)
            return false;
    }

    return true;
}

bool CStreamingSync::ShouldCreateVehicleAt(int modelId, unsigned char createdBy, const CVector& pos)
{
    if (!IsAmbientVehicle(createdBy))
        return true;

    return DistanceToLocalPlayer(pos) < (IsAircraftModel(modelId) ? AIRCRAFT_STREAM_IN : VEHICLE_STREAM_IN);
}

bool CStreamingSync::ShouldCreatePedAt(unsigned char createdBy, const CVector& pos)
{
    if (!IsAmbientPed(createdBy))
        return true;

    return DistanceToLocalPlayer(pos) < PED_STREAM_IN;
}

// a vehicle can go away only together with its occupants, and only if all of them are remote network peds
static bool CanStreamOutOccupants(CVehicle* vehicle)
{
    CPed* occupants[9] = {vehicle->m_pDriver};
    for (int i = 0; i < 8; i++)
        occupants[i + 1] = vehicle->m_apPassengers[i];

    for (CPed* ped : occupants)
    {
        if (!ped)
            continue;

        if (ped->IsPlayer())
            return false;

        CNetworkPed* networkPed = CNetworkPedManager::GetPed(ped);
        if (!networkPed || networkPed->m_bSyncing)
            return false;
    }

    return true;
}

static void StreamOutOccupants(CVehicle* vehicle)
{
    CPed* occupants[9] = {vehicle->m_pDriver};
    for (int i = 0; i < 8; i++)
        occupants[i + 1] = vehicle->m_apPassengers[i];

    for (CPed* ped : occupants)
    {
        if (!ped)
            continue;

        if (CNetworkPed* networkPed = CNetworkPedManager::GetPed(ped))
        {
            networkPed->StreamOut();
            // keep the record next to the vehicle, its next driver/passenger update puts it back inside
            networkPed->m_vecLastPos = vehicle->GetPosition();
        }
    }
}

void CStreamingSync::Process()
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return;

    // first player inside an interior owns its peds; every other one is his guest
    CInteriorOwnership::Process();

    for (CNetworkVehicle* networkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        // train records are owned by CTrainSync: no stream in/out (CreateVehicle refuses trains)
        if (Packets::Trains::IsTrainModel(networkVehicle->m_nModelId))
            continue;

        bool aircraft = IsAircraftModel(networkVehicle->m_nModelId);
        float streamIn = aircraft ? AIRCRAFT_STREAM_IN : VEHICLE_STREAM_IN;
        float streamOut = aircraft ? AIRCRAFT_STREAM_OUT : VEHICLE_STREAM_OUT;

        if (networkVehicle->m_bStreamedOut)
        {
            // a vehicle that became a mission vehicle (or ours) must exist; wrecks are not created again
            bool mustExist = networkVehicle->m_bSyncing || !IsAmbientVehicle(networkVehicle->m_nCreatedBy);
            if (mustExist || (networkVehicle->m_fLastHealth > 0.0f &&
                                 DistanceToLocalPlayer(networkVehicle->m_vecLastPos) < streamIn &&
                                 !IsSpotTakenByVehicle(networkVehicle->m_vecLastPos)))
                networkVehicle->StreamIn();
            continue;
        }

        if (networkVehicle->m_bSyncing || !IsAmbientVehicle(networkVehicle->m_nCreatedBy))
            continue;

        CVehicle* vehicle = networkVehicle->m_pVehicle;
        if (!vehicle || !vehicle->IsVTableValid() || !vehicle->m_matrix)
            continue;

        if (vehicle == localPlayer->m_pVehicle)
            continue;

        if (DistanceToLocalPlayer(vehicle->m_matrix->pos) > streamOut && CanStreamOutOccupants(vehicle))
        {
            StreamOutOccupants(vehicle);
            networkVehicle->StreamOut();
        }
    }

    for (CNetworkPed* networkPed : CNetworkPedManager::m_pPeds)
    {
        // our own peds are never suppressed
        if (networkPed->m_bSyncing)
        {
            networkPed->m_bSuppressedDuplicate = false;
            continue;
        }

        CPed* ped = networkPed->m_pPed;

        // never suppress network player peds
        if (ped && ped->IsVTableValid() && ped->IsPlayer())
            continue;

        if (ped && ped->IsVTableValid() && ped->m_matrix && !ped->m_nPedFlags.bInVehicle)
            LearnRemotePedArea(networkPed, ped, localPlayer);

        // owner: the population of this interior is ours, a guest must not add another one (tie backup)
        if (CInteriorOwnership::ShouldSuppressRemotePed(networkPed))
        {
            networkPed->SuppressDuplicate(CInteriorOwnership::GetLocalArea());
            continue;
        }

        // we are a guest (or outside): the owner peds are shown normally
        networkPed->m_bSuppressedDuplicate = false;

        if (networkPed->m_bStreamedOut)
        {
            // a ped that became a mission ped (or ours) must exist; dead peds are not created again
            bool mustExist = networkPed->m_bSyncing || !IsAmbientPed(networkPed->m_nCreatedBy);
            if (mustExist ||
                (networkPed->m_fHealth > 0.0f && DistanceToLocalPlayer(networkPed->m_vecLastPos) < PED_STREAM_IN))
            {
                networkPed->StreamIn();

                // D bug 3: the copy applies the last state its syncer sent (it is only resent every 3 s)
                if (networkPed->m_pPed != nullptr)
                    networkPed->ApplySavedTaskState();
            }
            continue;
        }

        if (!IsAmbientPed(networkPed->m_nCreatedBy))
            continue;

        ped = networkPed->m_pPed;
        if (!ped || !ped->IsVTableValid() || !ped->m_matrix)
            continue;

        // peds in vehicles go away together with their vehicle
        if (ped->m_nPedFlags.bInVehicle)
            continue;

        if (DistanceToLocalPlayer(ped->GetPosition()) > PED_STREAM_OUT)
            networkPed->StreamOut();
    }
}
