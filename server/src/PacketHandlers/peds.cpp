#include "network/packets/peds.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CHandover.h"
#include <cmath>

static float PopulationDistanceSqr(const CVector& a, const CVector& b)
{
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x*x + y*y + z*z;
}

PACKET_HANDLER(ePacketType::INTERIOR_POPULATION, Packets::Peds::InteriorPopulation* packet, CNetworkPlayer* sender)
{
    static uint64_t order = 0;
    const CVector pos = packet->pos;
    const bool changed = sender->m_nPopulationArea != packet->area ||
        PopulationDistanceSqr(sender->m_vecPopulationPos, pos) >= 22500.0f ||
        g_serverTime - sender->m_nPopulationSeen > 3000;
    if (changed || sender->m_nPopulationOrder == 0)
    {
        sender->m_nPopulationOrder = ++order;
        sender->m_vecPopulationPos = pos;
    }
    sender->m_nPopulationArea = packet->area;
    sender->m_nPopulationSeen = g_serverTime;

    // Recompute for every surviving member, including the room the sender just left.
    for (auto member : CNetworkPlayerManager::m_pPlayers)
    {
        if (!member->m_nPopulationArea || g_serverTime - member->m_nPopulationSeen > 3000)
            continue;
        CNetworkPlayer* owner = member;
        for (auto candidate : CNetworkPlayerManager::m_pPlayers)
        {
            if (candidate->m_nPopulationArea == member->m_nPopulationArea &&
                g_serverTime - candidate->m_nPopulationSeen <= 3000 &&
                PopulationDistanceSqr(candidate->m_vecPopulationPos, member->m_vecPopulationPos) < 22500.0f &&
                candidate->m_nPopulationOrder < owner->m_nPopulationOrder)
                owner = candidate;
        }
        Packets::Peds::InteriorPopulation state{};
        state.area = member->m_nPopulationArea;
        state.pos = member->m_vecPopulationPos;
        state.owner = owner->m_playerId;
        GetPacketFactory().Send(state, member);
        // Move only this room's population. Script mission actors never enter this path.
        for (auto ped : CNetworkPedManager::m_pPeds)
        {
            if (ped->m_pSyncer == owner || (!ped->m_bInteriorPopulation && ped->m_nCreatedBy == MISSION_CHAR) ||
                ped->m_nAreaCode != state.area || PopulationDistanceSqr(ped->m_vecPos, member->m_vecPopulationPos) >= 22500.0f)
                continue;
            logger::info("interior-transfer ped=%s owner=%u", to_string(ped->m_pedId).c_str(), owner->m_playerId.value());
            CHandover::AssignPed(ped, owner);
        }
    }
}

PACKET_HANDLER(ePacketType::PED_SPAWN, Packets::Peds::PedSpawn* pPedSpawn, CNetworkPlayer* pNetworkPlayer)
{
    static char allowedSpecialActors[52][8] = {"ANDRE", "BBTHIN", "BB", "CAT", "CESAR", "COPGRL1", "COPGRL2", "COPGRL3",
        "CLAUDE", "CROGRL1", "CROGRL2", "CROGRL3", "DWAYNE", "EMMET", "FORELLI", "GANGRL1", "GANGRL2", "GANGRL3",
        "GUNGRL1", "GUNGRL2", "GUNGRL3", "HERN", "JANITOR", "JETHRO", "JIZZY", "KENDL", "MACCER", "MADDOGG", "MECGRL1",
        "MECGRL2", "MECGRL3", "NURGRL1", "NURGRL2", "NURGRL3", "OGLOC", "PAUL", "PULASKI", "ROSE", "RYDER1", "RYDER2",
        "RYDER3", "SINDACO", "SMOKE", "SMOKEV", "SUZIE", "SWEET", "TBONE", "TENPEN", "TORINO", "TRUTH", "WUZIMU",
        "ZERO"};

    if (pPedSpawn->modelId >= MODEL_SPECIAL01 && pPedSpawn->modelId <= MODEL_SPECIAL10)
    {
        bool isKnownSpecialModel = false;

        for (int i = 0; i < ARRAY_SIZE(allowedSpecialActors); i++)
        {
            if (strncasecmp(pPedSpawn->specialModelName, allowedSpecialActors[i], strlen(allowedSpecialActors[i])) == 0)
            {
                isKnownSpecialModel = true;
                break;
            }
        }

        // the list doesn't hold every special actor the missions load, so an unknown one is only logged
        // (blocking it would break a mission; the old check accepted everything by mistake)
        if (!isKnownSpecialModel)
        {
            logger::warn("%s created a ped with an unlisted special model '%.8s'", pNetworkPlayer->GetName().c_str(),
                pPedSpawn->specialModelName);
        }
    }

    // the id is allocated by its creator, a player can only create peds in its own id space
    if (pPedSpawn->pedid.creator() != pNetworkPlayer->m_playerId)
    {
        logger::warn("%s tried to create a ped with someone else's id %s", pNetworkPlayer->GetName().c_str(),
            to_string(pPedSpawn->pedid).c_str());
        return;
    }

    if (CNetworkPedManager::GetPed(pPedSpawn->pedid) != nullptr)
    {
        logger::warn("%s tried to create a ped with an id that is already in use %s",
            pNetworkPlayer->GetName().c_str(), to_string(pPedSpawn->pedid).c_str());
        return;
    }

    if (pNetworkPlayer->m_nPopulationArea &&
        (pPedSpawn->interiorPopulation || pPedSpawn->createdBy != MISSION_CHAR) &&
        PopulationDistanceSqr(pPedSpawn->pos, pNetworkPlayer->m_vecPopulationPos) < 22500.0f)
    {
        auto owner = pNetworkPlayer;
        for (auto candidate : CNetworkPlayerManager::m_pPlayers)
            if (candidate->m_nPopulationArea == pNetworkPlayer->m_nPopulationArea &&
                g_serverTime - candidate->m_nPopulationSeen <= 3000 &&
                PopulationDistanceSqr(candidate->m_vecPopulationPos, pNetworkPlayer->m_vecPopulationPos) < 22500.0f &&
                candidate->m_nPopulationOrder < owner->m_nPopulationOrder)
                owner = candidate;
        if (owner != pNetworkPlayer)
        {
            logger::info("interior-reject ped=%s sender=%u owner=%u", to_string(pPedSpawn->pedid).c_str(),
                pNetworkPlayer->m_playerId.value(), owner->m_playerId.value());
            Packets::Peds::PedRemove remove{};
            remove.pedid = pPedSpawn->pedid;
            GetPacketFactory().Send(remove, pNetworkPlayer);
            return;
        }
    }
    // EVENT can arrive before the player's unreliable area update.
    pPedSpawn->areaCode = pPedSpawn->interiorPopulation && pNetworkPlayer->m_nPopulationArea ?
        pNetworkPlayer->m_nPopulationArea : pNetworkPlayer->m_nAreaCode;
    GetPacketFactory().SendToAll(*pPedSpawn, pNetworkPlayer);

    CNetworkPed* pNetworkPed = new CNetworkPed(
        pPedSpawn->pedid, pNetworkPlayer, pPedSpawn->modelId, pPedSpawn->pedType, pPedSpawn->pos, pPedSpawn->createdBy);
    snprintf(pNetworkPed->m_szSpecialModelName, sizeof(pNetworkPed->m_szSpecialModelName), "%s",
        pPedSpawn->specialModelName);
    pNetworkPed->m_nAreaCode = pPedSpawn->areaCode;
    pNetworkPed->m_bInteriorPopulation = pPedSpawn->interiorPopulation;
    if (pPedSpawn->interiorPopulation && pNetworkPlayer->m_nPopulationArea)
        pNetworkPed->m_nAreaCode = pNetworkPlayer->m_nPopulationArea;

    CNetworkPedManager::Add(pNetworkPed);
}

PACKET_HANDLER(ePacketType::PED_REMOVE, Packets::Peds::PedRemove* pPedRemove, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedRemove->pedid);

    if (!pNetworkPed)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to remove someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    for (auto p : CNetworkPlayerManager::m_pPlayers)
    {
        // check if this player has claimed the ped
        if (std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed) != p->m_vPedClaims.end())
        {
            // assign ped's syncer to this player
            pNetworkPed->m_pSyncer = p;
            pNetworkPed->m_nSyncerChangedAt = g_serverTime;

            // send ASSIGN_PED packet to the new host player to notify them they now own the ped
            Packets::Peds::AssignPedSyncer assignPedPacket{};
            assignPedPacket.pedid = pNetworkPed->m_pedId;
            GetPacketFactory().Send(assignPedPacket, p);

            // send PED_SPAWN packet to the old syncer (peer) so it can respawn the ped locally
            Packets::Peds::PedSpawn pedSpawnPacket{};
            pedSpawnPacket.pedid = pNetworkPed->m_pedId;
            pedSpawnPacket.modelId = pNetworkPed->m_nModelId;
            pedSpawnPacket.pedType = pNetworkPed->m_nPedType;
            pedSpawnPacket.pos = pNetworkPed->m_vecPos;
            snprintf(pedSpawnPacket.specialModelName, sizeof(pedSpawnPacket.specialModelName), "%s",
                pNetworkPed->m_szSpecialModelName);
            pedSpawnPacket.createdBy = pNetworkPed->m_nCreatedBy;
            pedSpawnPacket.interiorPopulation = pNetworkPed->m_bInteriorPopulation;
            pedSpawnPacket.areaCode = pNetworkPed->m_nAreaCode;
            GetPacketFactory().Send(pedSpawnPacket, pNetworkPlayer);

            // remove the ped from this player's claim list to avoid duplicate claims
            auto it = std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed);
            if (it != p->m_vPedClaims.end())
            {
                p->m_vPedClaims.erase(it);
            }

            // exit after assigning new host and respawning ped
            return;
        }
    }

    // if nobody claimed the ped, remove it from all players' claim lists
    for (auto p : CNetworkPlayerManager::m_pPlayers)
    {
        auto it = std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed);
        if (it != p->m_vPedClaims.end())
        {
            p->m_vPedClaims.erase(it);
        }
    }

    GetPacketFactory().SendToAll(*pPedRemove, pNetworkPlayer);
    CNetworkPedManager::Remove(pNetworkPed);
    delete pNetworkPed;
}

// B1: the syncer streamed the ped out and offers it to another player; only the owner may release it
PACKET_HANDLER(ePacketType::PED_RELEASE, Packets::Peds::PedRelease* pPedRelease, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedRelease->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        // the sender is not the owner: a stale release from a player that already lost it is ignored
        return;
    }

    CHandover::ReleasePed(pNetworkPed, pNetworkPlayer, pPedRelease->pos, pPedRelease->heading, pPedRelease->areaCode,
        pPedRelease->reason);
}

// paramedic coop (CPatientHandover on the clients): only the mission owner of the patient (or its syncer, the first
// time) moves it; ENTER hands it to the player driving the ambulance, the rest runs on whoever simulates it now
PACKET_HANDLER(ePacketType::PED_PATIENT_TASK, Packets::Peds::PedPatientTask* pPatientTask, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pPed = CNetworkPedManager::GetPed(pPatientTask->pedid);
    if (pPed == nullptr || pPed->m_nCreatedBy != MISSION_CHAR)
        return;

    CNetworkPlayer* pOwner = pPed->m_pMissionOwner != nullptr ? pPed->m_pMissionOwner : pPed->m_pSyncer;
    if (pNetworkPlayer != pOwner)
        return;

    if (pPatientTask->action == Packets::Peds::PedPatientTask::ENTER)
    {
        CNetworkPlayer* pTarget = CNetworkPlayerManager::GetPlayer(pPatientTask->targetPlayerId);
        if (pTarget == nullptr)
            return;

        // back to the owner: an ordinary mission ped of his again
        pPed->m_pMissionOwner = pTarget != pNetworkPlayer ? pNetworkPlayer : nullptr;
        if (pPed->m_pSyncer != pTarget)
        {
            CHandover::AssignPed(pPed, pTarget);
            logger::info("patient %s handed to %s", to_string(pPed->m_pedId).c_str(), pTarget->GetName().c_str());
        }

        // the owner runs its own command; another player gets it to run on the ped he simulates now
        if (pTarget != pNetworkPlayer)
            GetPacketFactory().Send(*pPatientTask, pTarget);
        return;
    }

    if (pPed->m_pSyncer != nullptr && pPed->m_pSyncer != pNetworkPlayer)
        GetPacketFactory().Send(*pPatientTask, pPed->m_pSyncer);

    if (pPatientTask->action == Packets::Peds::PedPatientTask::RELEASE ||
        pPatientTask->action == Packets::Peds::PedPatientTask::EXPLODE)
        pPed->m_pMissionOwner = nullptr;
}

// an update from a player that does not sync the ped (anymore): drop it and, at most once a second per ped, tell him
// again that he doesn't sync it, so whatever path left two players thinking they own it heals by itself
static bool RejectNotSyncer(CNetworkPed* pPed, CNetworkPlayer* pNetworkPlayer, const char* szWhat)
{
    if (pPed->m_pSyncer == pNetworkPlayer)
        return false;

    if (g_serverTime - pPed->m_nLastWrongSyncerNotice >= 1000)
    {
        pPed->m_nLastWrongSyncerNotice = g_serverTime;

        Packets::Peds::AssignPedSyncer unassign{};
        unassign.pedid = pPed->m_pedId;
        unassign.syncing = false;
        GetPacketFactory().Send(unassign, pNetworkPlayer);

        // right after a syncer change the old one's updates still in flight are expected: only log the lasting ones
        if (g_serverTime - pPed->m_nSyncerChangedAt >= 3000)
        {
            logger::warn("%s tries to update (%s) someone else's ped %s (syncer %s): told him to stop",
                pNetworkPlayer->GetName().c_str(), szWhat, to_string(pPed->m_pedId).c_str(),
                pPed->m_pSyncer != nullptr ? pPed->m_pSyncer->GetName().c_str() : "none");
        }
    }
    return true;
}

PACKET_HANDLER(ePacketType::PED_ONFOOT, Packets::Peds::PedOnFoot* pPedOnFoot, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pPed = CNetworkPedManager::GetPed(pPedOnFoot->pedid);
    if (pPed)
    {
        if (RejectNotSyncer(pPed, pNetworkPlayer, "on foot"))
        {
            return;
        }
        // a NaN position would pass every distance check of handover-proximity and hand the ped to anyone
        if (!std::isfinite(pPedOnFoot->pos.x) || !std::isfinite(pPedOnFoot->pos.y) || !std::isfinite(pPedOnFoot->pos.z))
        {
            return;
        }

        pPed->m_vecPos = pPedOnFoot->pos;
        pPed->m_nOnFootAt = g_serverTime;
        pPed->m_fHeading = pPedOnFoot->currentRotation.m_angle;
        pPed->m_nMoveState = (uint8_t)pPedOnFoot->moveState;
        pPed->m_bAiming = pPedOnFoot->bAiming;
        pPed->m_nHealth = pPedOnFoot->healthSnapshot.iHealth;
        GetPacketFactory().SendToAll(*pPedOnFoot, pNetworkPlayer);
    }
}

PACKET_HANDLER(
    ePacketType::PED_DRIVER_UPDATE, Packets::Peds::PedDriverUpdate* pPedDriverUpdate, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedDriverUpdate->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (RejectNotSyncer(pNetworkPed, pNetworkPlayer, "driver"))
    {
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedDriverUpdate->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    pNetworkVehicle->m_bUsedByPed = true;
    pNetworkVehicle->m_vecPosition = pPedDriverUpdate->pos;
    pNetworkVehicle->m_vecRotation = pPedDriverUpdate->rot;
    pNetworkPed->m_vecPos = pPedDriverUpdate->pos;  // B1: the ped is in the car, keep its position fresh
    pNetworkPed->m_nOnFootAt = 0;

    GetPacketFactory().SendToAll(*pPedDriverUpdate, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_PASSENGER_UPDATE, Packets::Peds::PedPassengerSync* pPedPassengerSync,
    CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedPassengerSync->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (RejectNotSyncer(pNetworkPed, pNetworkPlayer, "passenger"))
    {
        return;
    }

    // B1: a passenger ped's position is the one of the vehicle it rides
    if (CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedPassengerSync->vehicleid))
    {
        pNetworkPed->m_vecPos = pNetworkVehicle->m_vecPosition;
    }
    pNetworkPed->m_nOnFootAt = 0;

    GetPacketFactory().SendToAll(*pPedPassengerSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_JACKED, Packets::Peds::PedJacked* pPedJacked, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedJacked->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (CNetworkVehicleManager::GetVehicle(pPedJacked->vehicleid) == nullptr)
    {
        return;
    }

    GetPacketFactory().SendToAll(*pPedJacked, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_SHOT_SYNC, Packets::Peds::PedShotSync* pPedShotSync, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedShotSync->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (RejectNotSyncer(pNetworkPed, pNetworkPlayer, "shoot"))
    {
        return;
    }

    GetPacketFactory().SendToAll(*pPedShotSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_ADD_TASK, Packets::Peds::PedTaskState* pPedTaskState, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedTaskState->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (RejectNotSyncer(pNetworkPed, pNetworkPlayer, "task"))
    {
        return;
    }

    GetPacketFactory().SendToAll(*pPedTaskState, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_SAY, Packets::Peds::PedSay* pPedSay, CNetworkPlayer* pNetworkPlayer)
{
    if (pPedSay->entity.entityType == NETWORK_ENTITY_TYPE_PLAYER)
    {
        pPedSay->entity.playerid = pNetworkPlayer->m_playerId;
    }
    else if (pPedSay->entity.entityType == NETWORK_ENTITY_TYPE_PED)
    {
        CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedSay->entity.pedid);
        if (pNetworkPed == nullptr)
        {
            return;
        }
    }

    GetPacketFactory().SendToAll(*pPedSay, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_CLAIM_ON_RELEASE, Packets::Peds::PedClaimOnRelease* pPedClaimOnRelease,
    CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedClaimOnRelease->pedid);

    if (pNetworkPed && pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        if (std::find(pNetworkPlayer->m_vPedClaims.begin(), pNetworkPlayer->m_vPedClaims.end(), pNetworkPed) ==
            pNetworkPlayer->m_vPedClaims.end())
        {
            pNetworkPlayer->m_vPedClaims.push_back(pNetworkPed);
        }
    }
}

PACKET_HANDLER(
    ePacketType::PED_CANCEL_CLAIM, Packets::Peds::PedCancelClaim* pPedCancelClaim, CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedCancelClaim->pedid);

    if (pNetworkPed && pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        auto it = std::find(pNetworkPlayer->m_vPedClaims.begin(), pNetworkPlayer->m_vPedClaims.end(), pNetworkPed);
        if (it != pNetworkPlayer->m_vPedClaims.end())
        {
            pNetworkPlayer->m_vPedClaims.erase(it);
        }
    }
}

PACKET_HANDLER(ePacketType::PED_RESET_ALL_CLAIMS, Packets::Peds::PedResetAllClaims* pPedResetAllClaims,
    CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedResetAllClaims->pedid);

    if (pNetworkPed)
    {
        if (pNetworkPed->m_pSyncer == pNetworkPlayer || pNetworkPlayer->m_bIsHost)
        {
            if (pNetworkPlayer->m_bIsHost && pNetworkPed->m_pSyncer != nullptr)
            {
                // here was a bug that i have not understood yet

                Packets::Peds::AssignPedSyncer assignPedPacket{};
                assignPedPacket.pedid = pPedResetAllClaims->pedid;
                GetPacketFactory().Send(assignPedPacket, pNetworkPlayer);
                assignPedPacket.syncing = false;
                GetPacketFactory().Send(assignPedPacket, pNetworkPed->m_pSyncer);  // unassign old

                pNetworkPed->m_pSyncer = pNetworkPlayer;
                pNetworkPed->m_nSyncerChangedAt = g_serverTime;
            }

            for (auto p : CNetworkPlayerManager::m_pPlayers)
            {
                auto it = std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed);
                if (it != p->m_vPedClaims.end())
                {
                    GetPacketFactory().Send(*pPedResetAllClaims, p);
                    p->m_vPedClaims.erase(it);
                }
            }
        }
    }
}

PACKET_HANDLER(ePacketType::PED_TAKE_HOST, Packets::Peds::PedTakeHost* pPedTakeHost, CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedTakeHost->pedid);

    if (pNetworkPed)
    {
        if (pNetworkPed->m_pSyncer != pNetworkPlayer && pNetworkPlayer->m_bIsHost)
        {
            Packets::Peds::AssignPedSyncer assignPedPacket{};
            assignPedPacket.pedid = pPedTakeHost->pedid;
            GetPacketFactory().Send(assignPedPacket, pNetworkPlayer);

            auto it = std::find(pNetworkPlayer->m_vPedClaims.begin(), pNetworkPlayer->m_vPedClaims.end(), pNetworkPed);
            if (it != pNetworkPlayer->m_vPedClaims.end())
            {
                pNetworkPlayer->m_vPedClaims.erase(it);
            }

            assignPedPacket.syncing = false;
            GetPacketFactory().Send(assignPedPacket, pNetworkPed->m_pSyncer);

            if (pPedTakeHost->allowReturnToPreviousHost)
            {
                if (std::find(pNetworkPed->m_pSyncer->m_vPedClaims.begin(), pNetworkPed->m_pSyncer->m_vPedClaims.end(), pNetworkPed) ==
                    pNetworkPed->m_pSyncer->m_vPedClaims.end())
                {
                    pNetworkPed->m_pSyncer->m_vPedClaims.push_back(pNetworkPed);
                }
            }

            pNetworkPed->m_pSyncer = pNetworkPlayer;
            pNetworkPed->m_nSyncerChangedAt = g_serverTime;
        }
    }
}

// ped-damage-authority: a player hurt the copy of an NPC; only its syncer applies it
PACKET_HANDLER(ePacketType::PED_DAMAGE, Packets::Peds::PedDamage* pPedDamage, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedDamage->pedid);
    if (pNetworkPed == nullptr || pNetworkPed->m_pSyncer == nullptr || pNetworkPed->m_pSyncer == pNetworkPlayer)
    {
        return;
    }

    pPedDamage->attackerId = pNetworkPlayer->m_playerId;
    GetPacketFactory().Send(*pPedDamage, pNetworkPed->m_pSyncer);
}

// ped-damage-authority: the syncer of an NPC saw it hurt a player; only that player applies it
PACKET_HANDLER(ePacketType::PED_HIT_PLAYER, Packets::Peds::PedHitPlayer* pPedHitPlayer, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedHitPlayer->pedid);
    if (pNetworkPed == nullptr || pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        return;
    }

    CNetworkPlayer* pTarget = CNetworkPlayerManager::GetPlayer(pPedHitPlayer->playerid);
    if (pTarget != nullptr && pTarget != pNetworkPlayer)
    {
        GetPacketFactory().Send(*pPedHitPlayer, pTarget);
    }
}

// npc-anim-mirror: the animations of an NPC, from its syncer to everybody else
PACKET_HANDLER(ePacketType::PED_ANIM_SET, Packets::Peds::PedAnimSet* pPedAnimSet, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedAnimSet->pedid);
    if (pNetworkPed == nullptr || pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        return;
    }

    GetPacketFactory().SendToAll(*pPedAnimSet, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_RECRUIT, Packets::Peds::PedRecruit* packet, CNetworkPlayer* player)
{
    auto* ped = CNetworkPedManager::GetPed(packet->pedid);
    if (!ped)
        return;
    if (!packet->recruited)
    {
        if (ped->m_pRecruiter == player)
            ped->m_pRecruiter = nullptr;
        return;
    }
    if (ped->m_pRecruiter || !ped->m_pSyncer || ped->m_pMissionOwner ||
        ped->m_nCreatedBy != RANDOM_CHAR || ped->m_nPedType != PED_TYPE_GANG2 ||
        ped->m_nAreaCode != player->m_nAreaCode || !CHandover::IsPositionFresh(player) ||
        !(PopulationDistanceSqr(player->m_vecPos, ped->m_vecPos) <= 144.0f))
        return;

    int recruits = 0;
    for (const auto* other : CNetworkPedManager::m_pPeds)
        if (other->m_pRecruiter == player)
            ++recruits;
    if (recruits >= 7)
        return;

    ped->m_pRecruiter = player;
    if (ped->m_pSyncer != player)
        CHandover::AssignPed(ped, player);
}
