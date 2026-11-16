#include "stdafx.h"
#include "CHandover.h"

#include "network/packets/peds.h"
#include "network/packets/vehicles.h"
#include "network/packets/train.h"
#include "CCoopMission.h"

namespace
{
// a player whose last position is older than this is not a candidate
constexpr uint32_t FRESH_POSITION_MS = 5000;
// a second handover of the same entity within this window is refused (anti ping-pong)
constexpr uint32_t HANDOVER_COOLDOWN_MS = 5000;
// peds this close to a handed over vehicle travel with it (its occupants)
constexpr float OCCUPANT_HANDOVER_RADIUS = 6.0f;

void MarkTried(std::vector<CNetworkPlayer*>& vTried, CNetworkPlayer* pPlayer)
{
    if (pPlayer == nullptr)
        return;

    if (std::find(vTried.begin(), vTried.end(), pPlayer) == vTried.end())
        vTried.push_back(pPlayer);
}

template <typename T>
void ErasePointer(std::vector<T*>& vItems, T* pItem)
{
    auto it = std::find(vItems.begin(), vItems.end(), pItem);
    if (it != vItems.end())
        vItems.erase(it);
}

void SendPedSpawn(CNetworkPed* pPed, CNetworkPlayer* pTarget)
{
    Packets::Peds::PedSpawn packet{};
    packet.pedid = pPed->m_pedId;
    packet.modelId = pPed->m_nModelId;
    packet.pedType = pPed->m_nPedType;
    packet.pos = pPed->m_vecPos;
    snprintf(packet.specialModelName, sizeof(packet.specialModelName), "%s", pPed->m_szSpecialModelName);
    packet.createdBy = pPed->m_nCreatedBy;
    packet.interiorPopulation = pPed->m_bInteriorPopulation;
    packet.areaCode = pPed->m_nAreaCode;
    GetPacketFactory().Send(packet, pTarget);
}

// tell the chosen player he syncs the ped from now on; takeover tells him to create it at pos if he doesn't have it
void AssignPedTo(CNetworkPed* pPed, CNetworkPlayer* pNewSyncer, const CVector& vecPos, float heading)
{
    // the old syncer may still have it (MoveNearbyPedsTo: the occupants of a vehicle it released, it never released
    // them): it must stop sending its updates, as ReassignSyncer / PED_TAKE_HOST already tell it
    CNetworkPlayer* pOldSyncer = pPed->m_pSyncer;
    if (pOldSyncer != nullptr && pOldSyncer != pNewSyncer &&
        std::find(CNetworkPlayerManager::m_pPlayers.begin(), CNetworkPlayerManager::m_pPlayers.end(), pOldSyncer) !=
            CNetworkPlayerManager::m_pPlayers.end())
    {
        Packets::Peds::AssignPedSyncer unassign{};
        unassign.pedid = pPed->m_pedId;
        unassign.syncing = false;
        GetPacketFactory().Send(unassign, pOldSyncer);
    }

    if (pPed->m_pRecruiter != pNewSyncer)
        pPed->m_pRecruiter = nullptr;
    pPed->m_pSyncer = pNewSyncer;
    pPed->m_nLastHandoverTime = g_serverTime;
    pPed->m_nSyncerChangedAt = g_serverTime;

    Packets::Peds::AssignPedSyncer packet{};
    packet.pedid = pPed->m_pedId;
    packet.syncing = true;
    packet.takeover = true;
    packet.pos = vecPos;
    packet.heading = heading;
    GetPacketFactory().Send(packet, pNewSyncer);
}

void RemovePed(CNetworkPed* pPed, CNetworkPlayer* pIgnore)
{
    Packets::Peds::PedRemove packet{};
    packet.pedid = pPed->m_pedId;
    GetPacketFactory().SendToAll(packet, pIgnore);

    for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        ErasePointer(pPlayer->m_vPedClaims, pPed);
    }

    CNetworkPedManager::Remove(pPed);
    delete pPed;
}

void AssignVehicleTo(CNetworkVehicle* pVehicle, CNetworkPlayer* pNewSyncer, const CVector& vecPos)
{
    pVehicle->m_pSyncer = pNewSyncer;
    pVehicle->m_nLastHandoverTime = g_serverTime;

    Packets::Vehicles::AssignVehicleSyncer packet{};
    packet.vehicleid = pVehicle->m_vehicleId;
    packet.syncing = true;
    packet.takeover = true;
    packet.pos = vecPos;
    packet.heading = 0.0f;
    GetPacketFactory().Send(packet, pNewSyncer);
}

void RemoveVehicle(CNetworkVehicle* pVehicle, CNetworkPlayer* pIgnore)
{
    Packets::Vehicles::VehicleRemove packet{};
    packet.vehicleid = pVehicle->m_vehicleId;
    GetPacketFactory().SendToAll(packet, pIgnore);

    CNetworkVehicleManager::Remove(pVehicle);
    delete pVehicle;
}

bool VehicleHasPlayer(const CNetworkVehicle* pVehicle)
{
    for (CNetworkPlayer* pOccupant : pVehicle->m_pPlayers)
    {
        if (pOccupant != nullptr)
            return true;
    }
    return false;
}

// squared 3D distance; CVector::Magnitude is a plugin_sa symbol the server does not link
float DistanceSqr(const CVector& a, const CVector& b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// peds the leaving/old owner still syncs and that sit in (or next to) the vehicle follow it to the same player
void MoveNearbyPedsTo(CNetworkPlayer* pOldSyncer, const CVector& vecPos, CNetworkPlayer* pNewSyncer)
{
    for (CNetworkPed* pPed : CNetworkPedManager::m_pPeds)
    {
        if (pPed->m_pSyncer != pOldSyncer)
            continue;

        if (DistanceSqr(pPed->m_vecPos, vecPos) >= OCCUPANT_HANDOVER_RADIUS * OCCUPANT_HANDOVER_RADIUS)
            continue;

        MarkTried(pPed->m_vTried, pOldSyncer);
        AssignPedTo(pPed, pNewSyncer, pPed->m_vecPos, 0.0f);
    }
}

bool HandoverCooldownPassed(uint32_t lastHandoverTime, uint8_t reason)
{
    // a rejection is the answer to the handover we just made: it must fall through to the next candidate
    if (reason == 1)
        return true;

    return lastHandoverTime == 0 || g_serverTime - lastHandoverTime >= HANDOVER_COOLDOWN_MS;
}

// tries to give the ped to a claimant, or to a nearby player. Returns false when nobody took it (caller removes it).
bool AssignPedIfPossible(
    CNetworkPed* pPed, CNetworkPlayer* pOwner, const CVector& vecPos, float heading, uint8_t areaCode, uint8_t reason)
{
    pPed->m_vecPos = vecPos;
    pPed->m_nAreaCode = areaCode;
    MarkTried(pPed->m_vTried, pOwner);

    // a paramedic patient another player let go (culled it, or could not create it) goes back to the player whose
    // mission owns it: removing it would fail his mission
    if (pPed->m_pMissionOwner != nullptr && pPed->m_pMissionOwner != pOwner)
    {
        AssignPedTo(pPed, pPed->m_pMissionOwner, vecPos, heading);
        SendPedSpawn(pPed, pOwner);  // the one that let it go keeps a streamed out copy, as the claims path does
        return true;
    }

    // a player that claimed the ped gets it, exactly like the PED_REMOVE handler does today
    for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        auto it = std::find(pPlayer->m_vPedClaims.begin(), pPlayer->m_vPedClaims.end(), pPed);
        if (it == pPlayer->m_vPedClaims.end())
            continue;

        pPlayer->m_vPedClaims.erase(it);
        AssignPedTo(pPed, pPlayer, vecPos, heading);
        SendPedSpawn(pPed, pOwner);  // the old owner keeps a streamed out copy, as the claims path did
        return true;
    }

    if (!CConfigManager::GetConfigHandover() || !HandoverCooldownPassed(pPed->m_nLastHandoverTime, reason))
        return false;

    // mission actors only live in the game running the mission: handing them to another player would leave an
    // idle ped there forever (claims above are the mission's own way to move them)
    if (pPed->m_nCreatedBy == MISSION_CHAR && !pPed->m_bInteriorPopulation)
        return false;

    CNetworkPlayer* pCandidate = CHandover::FindCandidate(
        pOwner, vecPos, areaCode, (float)CConfigManager::GetConfigHandoverPedRadius(), pPed->m_vTried);
    if (pCandidate == nullptr)
        return false;

    // same area means the candidate most likely already has a copy: only then it needs no PED_SPAWN
    bool candidateHadPed = pCandidate->m_nAreaCode == areaCode;
    AssignPedTo(pPed, pCandidate, vecPos, heading);
    if (!candidateHadPed)
        SendPedSpawn(pPed, pCandidate);
    return true;
}
}  // namespace

bool CHandover::IsPositionFresh(const CNetworkPlayer* pPlayer)
{
    return pPlayer->m_nPosTime != 0 && g_serverTime - pPlayer->m_nPosTime < FRESH_POSITION_MS;
}

CNetworkPlayer* CHandover::FindCandidate(CNetworkPlayer* pSender, const CVector& vecPos, uint8_t areaCode,
    float radius, const std::vector<CNetworkPlayer*>& vTried)
{
    CNetworkPlayer* pBest = nullptr;
    float radiusSqr = radius * radius;
    float bestDistanceSqr = radiusSqr;

    for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pPlayer == pSender)
            continue;

        if (std::find(vTried.begin(), vTried.end(), pPlayer) != vTried.end())
            continue;

        if (pPlayer->m_nAreaCode != areaCode || !IsPositionFresh(pPlayer))
            continue;

        float distanceSqr = DistanceSqr(pPlayer->m_vecPos, vecPos);
        if (distanceSqr >= radiusSqr)
            continue;

        if (pBest == nullptr || distanceSqr < bestDistanceSqr ||
            (distanceSqr == bestDistanceSqr && pPlayer->m_playerId < pBest->m_playerId))
        {
            pBest = pPlayer;
            bestDistanceSqr = distanceSqr;
        }
    }

    return pBest;
}

void CHandover::ReleasePed(
    CNetworkPed* pPed, CNetworkPlayer* pSender, const CVector& vecPos, float heading, uint8_t areaCode, uint8_t reason)
{
    if (AssignPedIfPossible(pPed, pSender, vecPos, heading, areaCode, reason))
        return;

    // nobody took it. Without handover this is today's PED_REMOVE (the sender already dropped it); with
    // handover on the sender keeps a streamed out record, so it must receive the REMOVE too.
    RemovePed(pPed, CConfigManager::GetConfigHandover() ? nullptr : pSender);
}

void CHandover::ReleaseVehicle(
    CNetworkVehicle* pVehicle, CNetworkPlayer* pSender, const CVector& vecPos, uint8_t areaCode, uint8_t reason)
{
    pVehicle->m_vecPosition = vecPos;
    pVehicle->m_nAreaCode = areaCode;
    MarkTried(pVehicle->m_vTried, pSender);

    if (CConfigManager::GetConfigHandover())
    {
        // a player inside keeps driving it: the release is ignored, the vehicle is not handed over
        if (VehicleHasPlayer(pVehicle))
            return;

        if (pVehicle->m_nCreatedBy != MISSION_VEHICLE && !Packets::Trains::IsTrainModel(pVehicle->m_nModelId) &&
            HandoverCooldownPassed(pVehicle->m_nLastHandoverTime, reason))
        {
            CNetworkPlayer* pCandidate = FindCandidate(pSender, vecPos, areaCode,
                (float)CConfigManager::GetConfigHandoverVehicleRadius(), pVehicle->m_vTried);
            if (pCandidate != nullptr)
            {
                AssignVehicleTo(pVehicle, pCandidate, vecPos);
                MoveNearbyPedsTo(pSender, vecPos, pCandidate);
                return;
            }
        }
    }

    RemoveVehicle(pVehicle, CConfigManager::GetConfigHandover() ? nullptr : pSender);
}

void CHandover::AssignPed(CNetworkPed* pPed, CNetworkPlayer* pNewSyncer)
{
    AssignPedTo(pPed, pNewSyncer, pPed->m_vecPos, 0.0f);
}

void CHandover::HandoverPedsFrom(CNetworkPlayer* pPlayer)
{
    bool handover = CConfigManager::GetConfigHandover();

    size_t i = 0;
    while (i < CNetworkPedManager::m_pPeds.size())
    {
        CNetworkPed* pPed = CNetworkPedManager::m_pPeds[i];
        if (pPed->m_pSyncer != pPlayer)
        {
            ++i;
            continue;
        }

        // a paramedic patient goes back to the player whose mission owns it (removing it would fail his mission)
        if (pPed->m_pMissionOwner != nullptr && pPed->m_pMissionOwner != pPlayer)
        {
            AssignPedTo(pPed, pPed->m_pMissionOwner, pPed->m_vecPos, 0.0f);
            ++i;
            continue;
        }

        if (handover && AssignPedIfPossible(pPed, pPlayer, pPed->m_vecPos, 0.0f, pPed->m_nAreaCode, 0))
        {
            ++i;
            continue;
        }

        // nobody wants it: remove it as before (RemovePed erases it, the next ped takes this index)
        RemovePed(pPed, pPlayer);
    }
}

void CHandover::HandoverVehiclesFrom(CNetworkPlayer* pPlayer)
{
    bool handover = CConfigManager::GetConfigHandover();

    size_t i = 0;
    while (i < CNetworkVehicleManager::m_pVehicles.size())
    {
        CNetworkVehicle* pVehicle = CNetworkVehicleManager::m_pVehicles[i];
        if (pVehicle->m_pSyncer != pPlayer)
        {
            ++i;
            continue;
        }

        // a remaining player inside takes it (the leaving owner is not a candidate anymore)
        CNetworkPlayer* pOccupant = nullptr;
        for (CNetworkPlayer* pSeat : pVehicle->m_pPlayers)
        {
            if (pSeat != nullptr)
            {
                pOccupant = pSeat;
                break;
            }
        }

        bool isTrainCarriage = Packets::Trains::IsTrainModel(pVehicle->m_nModelId);

        if (handover && !isTrainCarriage && pOccupant != nullptr)
        {
            AssignVehicleTo(pVehicle, pOccupant, pVehicle->m_vecPosition);
            MoveNearbyPedsTo(pPlayer, pVehicle->m_vecPosition, pOccupant);
            ++i;
            continue;
        }

        CNetworkPlayer* pCandidate = nullptr;
        if (handover && !isTrainCarriage && pVehicle->m_nCreatedBy != MISSION_VEHICLE)
            pCandidate = FindCandidate(pPlayer, pVehicle->m_vecPosition, pVehicle->m_nAreaCode,
                (float)CConfigManager::GetConfigHandoverVehicleRadius(), pVehicle->m_vTried);

        if (pCandidate != nullptr)
        {
            AssignVehicleTo(pVehicle, pCandidate, pVehicle->m_vecPosition);
            MoveNearbyPedsTo(pPlayer, pVehicle->m_vecPosition, pCandidate);
            ++i;
            continue;
        }

        // nobody wants it: remove it as before (RemoveVehicle erases it, the next one takes this index)
        RemoveVehicle(pVehicle, pPlayer);
    }
}

void CHandover::ForgetPlayer(CNetworkPlayer* pPlayer)
{
    for (CNetworkPed* pPed : CNetworkPedManager::m_pPeds)
    {
        ErasePointer(pPed->m_vTried, pPlayer);
        if (pPed->m_pRecruiter == pPlayer)
            pPed->m_pRecruiter = nullptr;
        if (pPed->m_pMissionOwner == pPlayer)
            pPed->m_pMissionOwner = nullptr;
    }

    for (CNetworkVehicle* pVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        ErasePointer(pVehicle->m_vTried, pPlayer);
    }
}

namespace
{
// handover-proximity: the new player within this distance of the ped, its syncer at least this much farther than him
constexpr float PROXIMITY_NEAR = 25.0f;
constexpr float PROXIMITY_MARGIN = 25.0f;
// a ped that changed syncer (any path) is left alone this long: no ping-pong between two players walking around
constexpr uint32_t PROXIMITY_COOLDOWN_MS = 10000;
constexpr uint32_t PROXIMITY_INTERVAL_MS = 500;
// the syncer's last PED_ONFOOT is this recent: the ped is on foot (not in a vehicle, not streamed out)
constexpr uint32_t PROXIMITY_ONFOOT_FRESH_MS = 1500;
constexpr int PROXIMITY_MAX_PER_PASS = 4;

bool IsClaimed(CNetworkPed* pPed)
{
    for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (std::find(pPlayer->m_vPedClaims.begin(), pPlayer->m_vPedClaims.end(), pPed) != pPlayer->m_vPedClaims.end())
            return true;
    }
    return false;
}

// a calm street ped nobody's script or mission holds: what the population would make again anyway. Its new owner
// gives it the wander task (ASSIGN_PED takeover), so a ped running, fighting or aiming keeps its owner
bool CanMoveByProximity(CNetworkPed* pPed)
{
    return pPed->m_pSyncer != nullptr && pPed->m_nCreatedBy != MISSION_CHAR && !pPed->m_bInteriorPopulation &&
        pPed->m_nAreaCode == 0 && pPed->m_pMissionOwner == nullptr && pPed->m_pRecruiter == nullptr &&
        pPed->m_nPedType != PED_TYPE_COP && pPed->m_nPedType != PED_TYPE_MEDIC && pPed->m_nPedType != PED_TYPE_FIREMAN &&
        pPed->m_nOnFootAt != 0 && g_serverTime - pPed->m_nOnFootAt < PROXIMITY_ONFOOT_FRESH_MS &&
        pPed->m_nMoveState <= PEDMOVE_WALK && !pPed->m_bAiming && pPed->m_nHealth > 0 &&
        g_serverTime - pPed->m_nSyncerChangedAt >= PROXIMITY_COOLDOWN_MS &&
        (pPed->m_nLastHandoverTime == 0 || g_serverTime - pPed->m_nLastHandoverTime >= PROXIMITY_COOLDOWN_MS) &&
        // the mission script of the host may hold a handle to a street ped (GET_RANDOM_CHAR_IN_ZONE...)
        !(CCoopMission::IsScriptAuthority(pPed->m_pSyncer) && CCoopMission::IsAuthorityOnMission()) && !IsClaimed(pPed);
}
}  // namespace

void CHandover::ProcessProximity()
{
    static uint32_t lastPass = 0;
    if (g_serverTime - lastPass < PROXIMITY_INTERVAL_MS)
        return;
    lastPass = g_serverTime;
    if (!CConfigManager::GetConfigHandover() || !CConfigManager::GetConfigHandoverProximity() ||
        CNetworkPlayerManager::m_pPlayers.size() < 2)
        return;

    int moved = 0;
    for (CNetworkPed* pPed : CNetworkPedManager::m_pPeds)
    {
        if (moved >= PROXIMITY_MAX_PER_PASS)
            break;
        if (pPed == nullptr || !CanMoveByProximity(pPed))
            continue;

        CNetworkPlayer* pSyncer = pPed->m_pSyncer;
        // a syncer inside an interior or without a fresh position is as good as far away
        const float syncerDistSqr = pSyncer->m_nAreaCode == 0 && IsPositionFresh(pSyncer)
            ? DistanceSqr(pSyncer->m_vecPos, pPed->m_vecPos) : 1e12f;
        CNetworkPlayer* pCandidate = FindCandidate(pSyncer, pPed->m_vecPos, 0, PROXIMITY_NEAR, {});
        if (pCandidate == nullptr)
            continue;
        const float candidateDist = std::sqrt(DistanceSqr(pCandidate->m_vecPos, pPed->m_vecPos));
        const float farEnough = candidateDist + PROXIMITY_MARGIN;
        if (syncerDistSqr < farEnough * farEnough)
            continue;

        CServerLog::Printf("[handover] proximity ped %s: %s -> %s (%.0f m)\n", to_string(pPed->m_pedId).c_str(),
            pSyncer->GetName().c_str(), pCandidate->GetName().c_str(), candidateDist);
        AssignPedTo(pPed, pCandidate, pPed->m_vecPos, pPed->m_fHeading);
        moved++;
    }
}
