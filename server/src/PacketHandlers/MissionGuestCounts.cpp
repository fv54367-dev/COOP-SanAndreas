#include "stdafx.h"
#include "CCoopMission.h"
#include "network/packet_handler.h"
#include "network/packets/mission_guest_counts.h"

PACKET_HANDLER(ePacketType::MISSION_GUEST_COUNTS, Packets::Scripts::MissionGuestCounts* packet,
    CNetworkPlayer* sender)
{
    if (!packet->epoch || packet->mission < 0 || sender->m_bIndividualMode ||
        !CCoopMission::CanReceiveMissionPacket(sender))
        return;
    packet->senderPlayerid = sender->m_playerId;
    if (packet->request)
    {
        if (!CCoopMission::IsScriptAuthority(sender) || !sender->m_bOnMission)
            return;
        packet->owner = sender->m_playerId;
        for (auto player : CNetworkPlayerManager::m_pPlayers)
            if (player != sender && !player->m_bIndividualMode && CCoopMission::CanReceiveMissionPacket(player))
                GetPacketFactory().Send(*packet, player);
    }
    else
    {
        auto owner = CNetworkPlayerManager::GetPlayer(packet->owner);
        if (owner && owner != sender && !owner->m_bIndividualMode && owner->m_bOnMission &&
            CCoopMission::IsScriptAuthority(owner) && CCoopMission::CanReceiveMissionPacket(owner))
            GetPacketFactory().Send(*packet, owner);
    }
}
