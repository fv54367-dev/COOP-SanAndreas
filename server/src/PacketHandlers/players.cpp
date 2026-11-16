#include "network/packets/players.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "CPersistence.h"
#include "CStorySave.h"
#include "CSaveManager.h"

PACKET_HANDLER(
    ePacketType::PLAYER_ONFOOT_UPDATE, Packets::Players::OnFootUpdate* pOnFootUpdate, CNetworkPlayer* pNetworkPlayer)
{
    pOnFootUpdate->senderPlayerid = pNetworkPlayer->m_playerId;

    // B1: remember where the player is so a released ped/vehicle can be handed to the nearest one
    pNetworkPlayer->m_bSocialAlive = pOnFootUpdate->healthSnapshot.iHealth > 0;
    // the key packet only travels when the pad changes: a stale/default 'false' made every F9 pair request fail
    pNetworkPlayer->m_bSocialControls = !pOnFootUpdate->keySnapshot.controlsDisabled;
    pNetworkPlayer->m_vecPos = pOnFootUpdate->vecPos;
    pNetworkPlayer->m_nAreaCode = pOnFootUpdate->areaCode;
    pNetworkPlayer->m_nPosTime = g_serverTime;

    GetPacketFactory().SendToAll(*pOnFootUpdate, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_KEY_SYNC, Packets::Players::KeyPressed* pKeyPressed, CNetworkPlayer* pNetworkPlayer)
{
    pNetworkPlayer->m_bSocialControls = !pKeyPressed->keySnapshot.controlsDisabled;
    pKeyPressed->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pKeyPressed, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_CAMERA_SYNC, Packets::Players::PlayerCameraSync* pPlayerCameraSync, CNetworkPlayer* pNetworkPlayer)
{
    pPlayerCameraSync->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pPlayerCameraSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_PARACHUTE_STATE, Packets::Players::PlayerParachuteState* pParachuteState,
    CNetworkPlayer* pNetworkPlayer)
{
    pParachuteState->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pParachuteState, pNetworkPlayer);
}

// player-anim-mirror: only to the players that can see him (same interior area, within 250 m); a player that has
// not sent his position yet counts as near. The sender is excluded.
PACKET_HANDLER(ePacketType::PLAYER_ANIM_MIRROR, Packets::Players::PlayerAnimMirror* pAnimMirror,
    CNetworkPlayer* pNetworkPlayer)
{
    pAnimMirror->senderPlayerid = pNetworkPlayer->m_playerId;

    for (CNetworkPlayer* pOther : CNetworkPlayerManager::m_pPlayers)
    {
        if (pOther == nullptr || pOther == pNetworkPlayer)
            continue;

        if (pOther->m_nPosTime != 0 && pNetworkPlayer->m_nPosTime != 0)
        {
            if (pOther->m_nAreaCode != pNetworkPlayer->m_nAreaCode)
                continue;
            const float dx = pOther->m_vecPos.x - pNetworkPlayer->m_vecPos.x;
            const float dy = pOther->m_vecPos.y - pNetworkPlayer->m_vecPos.y;
            if (dx * dx + dy * dy > 250.0f * 250.0f)
                continue;
        }

        GetPacketFactory().Send(*pAnimMirror, pOther);
    }
}

PACKET_HANDLER(ePacketType::SET_PLAYER_TASK,Packets::Players::SetPlayerTask* pSetPlayerTask, CNetworkPlayer* pNetworkPlayer)
{
    pSetPlayerTask->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pSetPlayerTask, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::ENEX_TRANSITION, Packets::Players::EnExTransition* pEnExTransition,
    CNetworkPlayer* pNetworkPlayer)
{
    pEnExTransition->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pEnExTransition, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_PLACE_WAYPOINT, Packets::Players::PlayerPlaceWaypoint* pPlayerPlaceWaypoint, CNetworkPlayer* pNetworkPlayer)
{
    pNetworkPlayer->m_waypointState = *pPlayerPlaceWaypoint;

    pPlayerPlaceWaypoint->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pPlayerPlaceWaypoint, pNetworkPlayer);
}

// A3/A12: the player died/respawned, so his seat must be released (the corpse is not in the vehicle anymore); if he
// was the syncer, the sync goes to the first remaining occupant, otherwise the vehicle would stay synced to him
static void ReleasePlayerVehicleSeat(CNetworkPlayer* pNetworkPlayer)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pNetworkPlayer->m_vehicleId);
    if (pNetworkVehicle == nullptr)
    {
        pNetworkPlayer->m_vehicleId = vehicleid_t();
        pNetworkPlayer->m_nSeatId = -1;
        return;
    }

    bool wasSyncer = pNetworkVehicle->m_pSyncer == pNetworkPlayer;
    pNetworkPlayer->RemoveFromVehicle();

    if (wasSyncer)
    {
        for (int i = 0; i < 8; i++)
        {
            if (pNetworkVehicle->m_pPlayers[i] != nullptr)
            {
                pNetworkVehicle->ReassignSyncer(pNetworkVehicle->m_pPlayers[i]);
                break;
            }
        }
    }
}

PACKET_HANDLER(ePacketType::RESPAWN_PLAYER, Packets::Players::RespawnPlayer* pRespawnPlayer, CNetworkPlayer* pNetworkPlayer)
{
    pRespawnPlayer->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pRespawnPlayer, pNetworkPlayer);

    ReleasePlayerVehicleSeat(pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_DEATH, Packets::Players::PlayerDeath* pPlayerDeath, CNetworkPlayer* pNetworkPlayer)
{
    pNetworkPlayer->m_bSocialAlive = false;
    pPlayerDeath->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pPlayerDeath, pNetworkPlayer);

    ReleasePlayerVehicleSeat(pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_BULLET_SHOT, Packets::Players::PlayerBulletShot* pPlayerBulletShot, CNetworkPlayer* pNetworkPlayer)
{
    pPlayerBulletShot->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pPlayerBulletShot, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::ADD_PROJECTILE, Packets::Players::AddProjectile* pAddProjectile, CNetworkPlayer* pNetworkPlayer)
{
    GetPacketFactory().SendToAll(*pAddProjectile, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_STATS, Packets::Players::PlayerStats* pPlayerStats, CNetworkPlayer* pNetworkPlayer)
{
    for (size_t i = 0; i < ARRAY_SIZE(pNetworkPlayer->m_afStats); i++)
    {
        pNetworkPlayer->m_afStats[i] = pPlayerStats->stats[i];
    }

    pNetworkPlayer->m_ucSyncFlags.bStatsModified = true;
    pPlayerStats->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pPlayerStats, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_MONEY, Packets::Players::PlayerMoney* pPlayerMoney, CNetworkPlayer* pNetworkPlayer)
{
    pPlayerMoney->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pPlayerMoney, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::GIVE_AMMO, Packets::Players::GiveAmmo* pGiveAmmo, CNetworkPlayer* pNetworkPlayer)
{
    if (pGiveAmmo->targetPlayerid == pNetworkPlayer->m_playerId)
        return;

    if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pGiveAmmo->targetPlayerid))
    {
        pGiveAmmo->senderPlayerid = pNetworkPlayer->m_playerId;
        GetPacketFactory().Send(*pGiveAmmo, pTargetPlayer);
    }
}

PACKET_HANDLER(ePacketType::TEST_COMMAND, Packets::Players::TestCommand* pTestCommand, CNetworkPlayer* pNetworkPlayer)
{
    // test mode only (server-config.ini `test-commands = 1`)
    if (!CConfigManager::GetConfigTestCommands() || pTestCommand->targetPlayerid == pNetworkPlayer->m_playerId)
        return;

    if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pTestCommand->targetPlayerid))
    {
        pTestCommand->senderPlayerid = pNetworkPlayer->m_playerId;
        GetPacketFactory().Send(*pTestCommand, pTargetPlayer);
    }
}

PACKET_HANDLER(ePacketType::PLAYER_PERSIST_STATE, Packets::Players::PlayerPersistState* pPersistState, CNetworkPlayer* pNetworkPlayer)
{
    if (!CPersistence::IsEnabled())
        return;

    // E bug 6: an empty answer (client loading, cutscene, dead) is still the answer of the save-all: write the state
    // when there is one and free the player from the pending list either way, or the requester would see a timeout
    if (pPersistState->hasData)
        CPersistence::Save(pNetworkPlayer->m_Name, pPersistState->data);

    CSaveManager::NotifySaved(pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::STORY_SAVE_CHUNK, Packets::Players::StorySaveChunk* pChunk, CNetworkPlayer* pNetworkPlayer)
{
    CStorySave::HandleChunk(pNetworkPlayer, pChunk);
}

PACKET_HANDLER(ePacketType::REBUILD_PLAYER, Packets::Players::RebuildPlayer* pRebuildPlayer, CNetworkPlayer* pNetworkPlayer)
{
    pNetworkPlayer->m_pPedClothesDesc = pRebuildPlayer->clothesDesc;
    pNetworkPlayer->m_ucSyncFlags.bClothesModified = true;
    pRebuildPlayer->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pRebuildPlayer, pNetworkPlayer);
}


// T79: seat authority belongs to the server, not to the supplied driver id.
#include "network/packets/gps.h"
PACKET_HANDLER(ePacketType::VEHICLE_GPS, Packets::Players::VehicleGps* packet, CNetworkPlayer* sender)
{
    auto* car = CNetworkVehicleManager::GetVehicle(sender->m_vehicleId);
    if (!car || sender->m_bIndividualMode || !sender->m_bSocialAlive || sender->m_nAreaCode ||
        sender->m_nSeatId != 0 || car->m_pPlayers[0] != sender || packet->vehicle != sender->m_vehicleId ||
        g_serverTime - sender->m_nPosTime > 2000) return;
    packet->driver = sender->m_playerId;
    for (int i = 1; i < 8; ++i) {
        auto* passenger = car->m_pPlayers[i];
        if (!passenger || passenger->m_bIndividualMode || !passenger->m_bSocialAlive ||
            passenger->m_vehicleId != car->m_vehicleId || passenger->m_nSeatId != i || passenger->m_nAreaCode ||
            g_serverTime - passenger->m_nPosTime > 2000) continue;
        GetPacketFactory().Send(*packet, passenger);
    }
}
