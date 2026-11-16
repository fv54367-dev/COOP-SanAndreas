#include "stdafx.h"
#include "CCoopMission.h"
#include "network/packet_handler.h"
#include "network/packets/mission_wait_notice.h"

// The "waiting for ..." notice: the script authority's, or START from a guest standing in a start marker the
// authority is not at (his launcher never asks then); to every other player that gets the mission packets
PACKET_HANDLER(ePacketType::MISSION_WAIT_NOTICE, Packets::Scripts::MissionWaitNotice* packet, CNetworkPlayer* sender)
{
    if (packet->kind >= Packets::Scripts::MissionWaitNotice::KIND_COUNT || sender->m_bIndividualMode ||
        (!CCoopMission::IsScriptAuthority(sender) && packet->kind != Packets::Scripts::MissionWaitNotice::START) ||
        !CCoopMission::CanReceiveMissionPacket(sender))
        return;
    packet->senderPlayerid = sender->m_playerId;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
        if (player != sender && !player->m_bIndividualMode && CCoopMission::CanReceiveMissionPacket(player))
            GetPacketFactory().Send(*packet, player);
}
