#include "network/packets/blips.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CCoopMission.h"

// F: the blips/checkpoints of a mission follow the script authority (the coop mission owner while it runs) and, with
// a coop mission running, they only reach its participants
PACKET_HANDLER(ePacketType::UPDATE_ENTITY_BLIP, Packets::Blips::UpdateEntityBlip* pUpdateEntityBlip, CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pUpdateEntityBlip->forWhoPlayerId))
        {
            if (CCoopMission::CanReceiveMissionPacket(pTargetPlayer))
            {
                GetPacketFactory().Send(*pUpdateEntityBlip, pTargetPlayer);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::REMOVE_ENTITY_BLIP, Packets::Blips::RemoveEntityBlip* pRemoveEntityBlip, CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pRemoveEntityBlip->forWhoPlayerId))
        {
            if (CCoopMission::CanReceiveMissionPacket(pTargetPlayer))
            {
                GetPacketFactory().Send(*pRemoveEntityBlip, pTargetPlayer);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::CLEAR_ENTITY_BLIPS, Packets::Blips::ClearEntityBlips* pClearEntityBlips, CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pClearEntityBlips->forWhoPlayerId))
        {
            if (CCoopMission::CanReceiveMissionPacket(pTargetPlayer))
            {
                GetPacketFactory().Send(*pClearEntityBlips, pTargetPlayer);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::UPDATE_CHECKPOINT, Packets::Blips::UpdateCheckpoint* pUpdateCheckpoint, CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pUpdateCheckpoint->forWhoPlayerId))
        {
            if (CCoopMission::CanReceiveMissionPacket(pTargetPlayer))
            {
                GetPacketFactory().Send(*pUpdateCheckpoint, pTargetPlayer);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::REMOVE_CHECKPOINT, Packets::Blips::RemoveCheckpoint* pRemoveCheckpoint, CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pRemoveCheckpoint->forWhoPlayerId))
        {
            if (CCoopMission::CanReceiveMissionPacket(pTargetPlayer))
            {
                GetPacketFactory().Send(*pRemoveCheckpoint, pTargetPlayer);
            }
        }
    }
}

// the mission's red cylinders: from the script authority to the players of its mission
PACKET_HANDLER(ePacketType::MISSION_AREA_MARKER, Packets::Blips::MissionAreaMarker* pMissionAreaMarker, CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
        CCoopMission::Relay(*pMissionAreaMarker, pNetworkPlayer, pNetworkPlayer);
}

// the race checkpoints of a mission: from the script authority to the players of its mission
PACKET_HANDLER(ePacketType::MISSION_RACE_CHECKPOINTS, Packets::Blips::MissionRaceCheckpoints* pRaceCheckpoints, CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
        CCoopMission::Relay(*pRaceCheckpoints, pNetworkPlayer, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::CREATE_STATIC_BLIP, Packets::Blips::StaticBlipsSnapshot* pCreateStaticBlip, CNetworkPlayer* pNetworkPlayer)
{
    // the world blips stay with the host
    if (!pNetworkPlayer->m_bIsHost)
        return;

    // the blips of the host's synced mission only go to the players of that mission: one that connected in the middle
    // of it (or plays individual) gets the snapshot without them
    Packets::Blips::StaticBlipsSnapshot worldOnly = *pCreateStaticBlip;
    worldOnly.countBlips = 0;
    for (uint32_t i = 0; i < pCreateStaticBlip->countBlips; i++)
    {
        if (!pCreateStaticBlip->blips[i].missionBlip)
            worldOnly.blips[worldOnly.countBlips++] = pCreateStaticBlip->blips[i];
    }

    for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pPlayer == pNetworkPlayer)
            continue;
        GetPacketFactory().Send(CCoopMission::CanReceiveMissionPacket(pPlayer) ? *pCreateStaticBlip : worldOnly, pPlayer);
    }
}
