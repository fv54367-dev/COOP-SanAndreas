#include "stdafx.h"
#include "CCoopMission.h"
#include "network/packet_handler.h"
#include "network/packets/home_invasion.h"

PACKET_HANDLER(ePacketType::HOME_INVASION, Packets::Scripts::HomeInvasion* packet, CNetworkPlayer* sender)
{
    if (!packet->epoch || !packet->sequence || sender->m_bIndividualMode ||
        !CCoopMission::CanReceiveMissionPacket(sender)) return;
    packet->senderPlayerid = sender->m_playerId;
    if (packet->snapshot)
    {
        if (!CCoopMission::IsScriptAuthority(sender) || !sender->m_bOnMission) return;
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
