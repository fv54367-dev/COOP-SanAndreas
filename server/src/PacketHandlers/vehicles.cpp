#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/vehicles.h"
#include "network/packets/train.h"
#include "CHandover.h"
#include "CCoopMission.h"

// the seat is held by another player: tell the loser to get out instead of stealing it back on every update
static void SendSeatDenied(CNetworkVehicle* pNetworkVehicle, int8_t seatid, CNetworkPlayer* pNetworkPlayer)
{
    Packets::Vehicles::VehicleSeatDenied packet{};
    packet.vehicleid = pNetworkVehicle->m_vehicleId;
    packet.seatid = seatid;
    GetPacketFactory().Send(packet, pNetworkPlayer);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_SPAWN, Packets::Vehicles::VehicleSpawn* pVehicleSpawn, CNetworkPlayer* pNetworkPlayer)
{
    // the id is allocated by its creator, a player can only create vehicles in its own id space
    if (pVehicleSpawn->vehicleid.creator() != pNetworkPlayer->m_playerId)
    {
        logger::warn("%s tried to create a vehicle with someone else's id %s", pNetworkPlayer->GetName().c_str(),
            to_string(pVehicleSpawn->vehicleid).c_str());
        return;
    }

    if (CNetworkVehicleManager::GetVehicle(pVehicleSpawn->vehicleid) != nullptr)
    {
        logger::warn("%s tried to create a vehicle with an id that is already in use %s",
            pNetworkPlayer->GetName().c_str(), to_string(pVehicleSpawn->vehicleid).c_str());
        return;
    }

    GetPacketFactory().SendToAll(*pVehicleSpawn, pNetworkPlayer);

    CNetworkVehicle* vehicle = new CNetworkVehicle(
        pVehicleSpawn->vehicleid, pVehicleSpawn->modelid, pVehicleSpawn->pos, pVehicleSpawn->rot.m_angle);

    vehicle->m_pSyncer = pNetworkPlayer;
    vehicle->m_nPrimaryColor = pVehicleSpawn->color1;
    vehicle->m_nSecondaryColor = pVehicleSpawn->color2;
    vehicle->m_nCreatedBy = pVehicleSpawn->createdBy;
    vehicle->m_nAreaCode = pNetworkPlayer->m_nAreaCode;

    CNetworkVehicleManager::Add(vehicle);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_REMOVE, Packets::Vehicles::VehicleRemove* pVehicleRemove, CNetworkPlayer* pNetworkPlayer)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(pVehicleRemove->vehicleid))
    {
        if (vehicle->m_pSyncer == pNetworkPlayer)
        {
            GetPacketFactory().SendToAll(*pVehicleRemove, pNetworkPlayer);

            CNetworkVehicleManager::Remove(vehicle);
            delete vehicle;
        }
        else
        {
            logger::warn("%s tried to delete someone else's vehicle", pNetworkPlayer->GetName().c_str());
        }
    }
}

// B1: the syncer streamed the vehicle out and offers it to another player; only the owner may release it
PACKET_HANDLER(ePacketType::VEHICLE_RELEASE, Packets::Vehicles::VehicleRelease* pVehicleRelease,
    CNetworkPlayer* pNetworkPlayer)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleRelease->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    if (pNetworkVehicle->m_pSyncer != pNetworkPlayer)
    {
        // the sender is not the owner: a stale release from a player that already lost it is ignored
        return;
    }

    CHandover::ReleaseVehicle(
        pNetworkVehicle, pNetworkPlayer, pVehicleRelease->pos, pVehicleRelease->areaCode, pVehicleRelease->reason);
}

PACKET_HANDLER(ePacketType::VEHICLE_IDLE_UPDATE, Packets::Vehicles::VehicleIdleUpdate* pVehicleIdleUpdate,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(pVehicleIdleUpdate->vehicleid))
    {
        if (vehicle->m_pSyncer == pNetworkPlayer)
        {
            vehicle->m_bUsedByPed = false;
            vehicle->m_vecPosition = pVehicleIdleUpdate->pos;
            vehicle->m_vecRotation = pVehicleIdleUpdate->rot;

            // the syncer says the car has no driver: drop a stale driver seat (an exit whose driver update was
            // still in flight, a dead player...) so the syncer can be handed to a real passenger
            if (vehicle->m_pPlayers[0] == pNetworkPlayer)
                vehicle->SetOccupant(0, nullptr);

            if (vehicle->m_jackedVictimId == pNetworkPlayer->m_playerId)
                vehicle->m_jackedVictimId = playerid_t();  // the victim of a jack is out of the seat already

            GetPacketFactory().SendToAll(*pVehicleIdleUpdate, pNetworkPlayer);
        }
        else
        {
            logger::warn("%s tried to update someone else's vehicle (idle)", pNetworkPlayer->GetName().c_str());
        }
    }
    else
    {
        logger::warn("%s tried to update a vehicle (idle) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_DRIVER_UPDATE, Packets::Vehicles::VehicleDriverUpdate* pVehicleDriverUpdate,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDriverUpdate->vehicleid))
    {
        // an announced jack: the victim is still seated in his own game for a few ms, but the seat is not his
        // anymore (it must not steal it back from the jacker, and it is not a seat conflict either)
        if (pNetworkVehicle->m_jackedVictimId.valid() &&
            pNetworkVehicle->m_jackedVictimId == pNetworkPlayer->m_playerId)
        {
            return;
        }

        // another player holds the driver seat: don't steal it, the sender gets out (client VEHICLE_SEAT_DENIED).
        // no log here: a denied client keeps sending these at ~35 Hz until it gets out
        if (pNetworkVehicle->IsSeatTakenByOther(0, pNetworkPlayer))
        {
            SendSeatDenied(pNetworkVehicle, -1, pNetworkPlayer);
            return;
        }

        pNetworkVehicle->SetOccupant(0, pNetworkPlayer);
        pNetworkVehicle->m_jackedVictimId = playerid_t();
        pNetworkVehicle->m_vecPosition = pVehicleDriverUpdate->pos;
        pNetworkVehicle->m_vecRotation = pVehicleDriverUpdate->rot;
        pNetworkVehicle->m_bUsedByPed = false;
        // a carriage keeps its syncer (the train authority): a friend sitting in a locomotive must not take
        // the train over (his leaving or restarting would remove it on the host's game too)
        if (!Packets::Trains::IsTrainModel(pNetworkVehicle->m_nModelId))
            pNetworkVehicle->ReassignSyncer(pNetworkPlayer);

        // B1: the server tracks where each player is (the car's position while driving)
        pNetworkPlayer->m_vecPos = pVehicleDriverUpdate->pos;
        pNetworkPlayer->m_nPosTime = g_serverTime;

        pVehicleDriverUpdate->senderPlayerid = pNetworkPlayer->m_playerId;
        GetPacketFactory().SendToAll(*pVehicleDriverUpdate, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to update a vehicle (driver) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(
    ePacketType::VEHICLE_ENTER, Packets::Vehicles::VehicleEnter* pVehicleEnter, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleEnter->vehicleid))
    {
        if (pVehicleEnter->bPassenger)
        {
            // seatid: 0..7 = passenger seat, internal m_pPlayers index is seatid + 1 (0 is the driver)
            if (pVehicleEnter->seatid < 0 || pVehicleEnter->seatid > 7)
                return;

            if (pVehicleEnter->seatid < 7)
            {
                int8_t seatid = pVehicleEnter->seatid + 1;
                if (pNetworkVehicle->IsSeatTakenByOther(seatid, pNetworkPlayer))
                {
                    SendSeatDenied(pNetworkVehicle, pVehicleEnter->seatid, pNetworkPlayer);
                    logger::warn("%s tried to enter a passenger seat occupied by another player",
                        pNetworkPlayer->GetName().c_str());
                    return;
                }
                pNetworkVehicle->SetOccupant(seatid, pNetworkPlayer);
            }
        }
        else
        {
            if (pNetworkVehicle->IsSeatTakenByOther(0, pNetworkPlayer))
            {
                SendSeatDenied(pNetworkVehicle, -1, pNetworkPlayer);
                logger::warn("%s tried to enter a vehicle driven by another player",
                    pNetworkPlayer->GetName().c_str());
                return;
            }
            pNetworkVehicle->SetOccupant(0, pNetworkPlayer);
        }

        pVehicleEnter->senderPlayerid = pNetworkPlayer->m_playerId;
        GetPacketFactory().SendToAll(*pVehicleEnter, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to enter a vehicle that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_EXIT, Packets::Vehicles::VehicleExit* pVehicleExit, CNetworkPlayer* pNetworkPlayer)
{
    pVehicleExit->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pVehicleExit, pNetworkPlayer);

    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pNetworkPlayer->m_vehicleId))
    {
        if (pNetworkPlayer->m_nSeatId >= 0)
        {
            pNetworkVehicle->SetOccupant(pNetworkPlayer->m_nSeatId, nullptr);
        }

        if (pNetworkVehicle->m_jackedVictimId == pNetworkPlayer->m_playerId)
            pNetworkVehicle->m_jackedVictimId = playerid_t();

        // the ex-driver must not keep syncing the car: the driver still inside, or the first passenger, takes it.
        // A carriage is always the authority's: its syncer never moves
        if (pNetworkVehicle->m_nCreatedBy != MISSION_VEHICLE && !pNetworkVehicle->m_bUsedByPed &&
            !Packets::Trains::IsTrainModel(pNetworkVehicle->m_nModelId))
            pNetworkVehicle->SyncToOccupant();
    }
}

PACKET_HANDLER(
    ePacketType::VEHICLE_DAMAGE, Packets::Vehicles::VehicleDamage* pVehicleDamage, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDamage->vehicleid))
    {
        pNetworkVehicle->m_damageManager = pVehicleDamage->damageManager;

        GetPacketFactory().SendToAll(*pVehicleDamage, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to damage a vehicle that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_COMPONENT_ADD, Packets::Vehicles::VehicleComponentAdd* pVehicleComponentAdd,
    CNetworkPlayer* pNetworkPlayer)
{
    // vehicle upgrade models are 1000..1193
    if (pVehicleComponentAdd->componentid < 1000 || pVehicleComponentAdd->componentid > 1193)
        return;

    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentAdd->vehicleid))
    {
        auto& components = pNetworkVehicle->m_pComponents;
        if (std::find(components.begin(), components.end(), pVehicleComponentAdd->componentid) == components.end())
            components.push_back(pVehicleComponentAdd->componentid);

        GetPacketFactory().SendToAll(*pVehicleComponentAdd, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (comp add) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_COMPONENT_REMOVE,
    Packets::Vehicles::VehicleComponentRemove* pVehicleComponentRemove, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentRemove->vehicleid))
    {
        auto it = std::find(pNetworkVehicle->m_pComponents.begin(), pNetworkVehicle->m_pComponents.end(),
            pVehicleComponentRemove->componentid);
        if (it != pNetworkVehicle->m_pComponents.end())
        {
            pNetworkVehicle->m_pComponents.erase(it);
        }

        GetPacketFactory().SendToAll(*pVehicleComponentRemove, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (comp remove) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_PASSENGER_UPDATE,
    Packets::Vehicles::VehiclePassengerUpdate* pVehiclePassengerUpdate, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehiclePassengerUpdate->vehicleid))
    {
        // a seatid of -1 is "the ped isn't in a passenger seat": it used to claim the seat of the driver
        if (pVehiclePassengerUpdate->seatid < 0 || pVehiclePassengerUpdate->seatid > 7)
            return;

        if (pVehiclePassengerUpdate->seatid < 7)
        {
            int8_t seatid = pVehiclePassengerUpdate->seatid + 1;
            if (pNetworkVehicle->IsSeatTakenByOther(seatid, pNetworkPlayer))
            {
                // no log here: the denied client keeps sending this at ~3 Hz until it gets out
                SendSeatDenied(pNetworkVehicle, pVehiclePassengerUpdate->seatid, pNetworkPlayer);
                return;
            }

            pNetworkVehicle->SetOccupant(seatid, pNetworkPlayer);
        }

        if (pNetworkVehicle->m_jackedVictimId == pNetworkPlayer->m_playerId)
            pNetworkVehicle->m_jackedVictimId = playerid_t();  // the victim sits somewhere else now

        // B1: the server tracks where each player is (the car's position while riding along)
        pNetworkPlayer->m_vecPos = pNetworkVehicle->m_vecPosition;
        pNetworkPlayer->m_nPosTime = g_serverTime;

        pVehiclePassengerUpdate->senderPlayerid = pNetworkPlayer->m_playerId;
        GetPacketFactory().SendToAll(*pVehiclePassengerUpdate, pNetworkPlayer);

        if (pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE ||
            Packets::Trains::IsTrainModel(pNetworkVehicle->m_nModelId))  // carriage: always the authority's
        {
            return;
        }

        // the driver syncs it when there is one: a passenger keeps syncing it only while nobody drives (a stale
        // driver seat left by an exit whose driver update was still in flight is healed here)
        if (!pNetworkVehicle->m_bUsedByPed)
            pNetworkVehicle->SyncToOccupant();
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (passenger) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::SET_VEHICLE_CREATED_BY, Packets::Vehicles::SetVehicleCreatedBy* pSetVehicleCreatedBy,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pSetVehicleCreatedBy->vehicleid))
        {
            pNetworkVehicle->m_nCreatedBy = pSetVehicleCreatedBy->createdBy;
            CCoopMission::Relay(*pSetVehicleCreatedBy, pNetworkPlayer, pNetworkPlayer, true);
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_DOORS, Packets::Vehicles::VehicleDoors* pVehicleDoors,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDoors->vehicleid))
    {
        if (pNetworkVehicle->m_pSyncer == pNetworkPlayer)
        {
            GetPacketFactory().SendToAll(*pVehicleDoors, pNetworkPlayer);
        }
        else
        {
            logger::warn("%s tried to sync someone else's vehicle (doors)", pNetworkPlayer->GetName().c_str());
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_HYDRAULIC_SYNC,
    Packets::Vehicles::VehicleHydraulicSync* pVehicleHydraulicSync, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleHydraulicSync->vehicleid))
    {
        if (pNetworkVehicle->m_pSyncer == pNetworkPlayer)
        {
            GetPacketFactory().SendToAll(*pVehicleHydraulicSync, pNetworkPlayer);
        }
        else
        {
            logger::warn("%s tried to sync someone else's vehicle (hydraulics)", pNetworkPlayer->GetName().c_str());
        }
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (hydraulics) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(
    ePacketType::PLAYER_JACKED, Packets::Vehicles::PlayerJacked* pPlayerJacked, CNetworkPlayer* pNetworkPlayer)
{
    if (CNetworkVehicleManager::GetVehicle(pPlayerJacked->vehicleid) == nullptr)
    {
        logger::warn("%s tried to jack a player from a vehicle that didn't exist", pNetworkPlayer->GetName().c_str());
        return;
    }

    if (pPlayerJacked->victimPlayerId == pNetworkPlayer->m_playerId ||
        CNetworkPlayerManager::GetPlayer(pPlayerJacked->victimPlayerId) == nullptr)
    {
        return;
    }

    pPlayerJacked->senderPlayerid = pNetworkPlayer->m_playerId;

    // the victim is leaving that seat: free it now, the jacker's first driver update may arrive before the exit
    // (otherwise it is taken for a second player stealing a seat and the jacker is sent out of the car)
    if (auto pVictim = CNetworkPlayerManager::GetPlayer(pPlayerJacked->victimPlayerId))
    {
        if (auto pVehicle = CNetworkVehicleManager::GetVehicle(pPlayerJacked->vehicleid))
            pVehicle->m_jackedVictimId = pVictim->m_playerId;

        pVictim->RemoveFromVehicle();
    }

    // the jacker also receives it: it keeps its local drag and only skips the victim warps
    GetPacketFactory().SendToAll(*pPlayerJacked);
}

PACKET_HANDLER(ePacketType::PLAYER_BEING_JACKED, Packets::Vehicles::PlayerBeingJacked* pPlayerBeingJacked,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CNetworkVehicleManager::GetVehicle(pPlayerBeingJacked->vehicleid) == nullptr)
        return;

    pPlayerBeingJacked->senderPlayerid = pNetworkPlayer->m_playerId;

    // the sender is being dragged out: free its seat, any driver update still in flight must not keep it
    if (auto pVehicle = CNetworkVehicleManager::GetVehicle(pPlayerBeingJacked->vehicleid))
        pVehicle->m_jackedVictimId = pNetworkPlayer->m_playerId;

    pNetworkPlayer->RemoveFromVehicle();

    GetPacketFactory().SendToAll(*pPlayerBeingJacked, pNetworkPlayer);
}
