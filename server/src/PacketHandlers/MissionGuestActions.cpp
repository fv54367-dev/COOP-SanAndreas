#include "stdafx.h"
#include "CCoopMission.h"
#include "network/packet_handler.h"
#include "network/packets/mission_guest_actions.h"
PACKET_HANDLER(ePacketType::MISSION_GUEST_ACTIONS, Packets::Scripts::MissionGuestActions* packet, CNetworkPlayer* sender)
{
    using P = Packets::Scripts::MissionGuestActions;
    if (!packet->epoch || packet->mission < 0 || sender->m_bIndividualMode ||
        !CCoopMission::CanReceiveMissionPacket(sender)) return;
    packet->senderPlayerid = sender->m_playerId;
    if (packet->kind == P::SESSION || packet->kind == P::BEGIN || packet->kind == P::END)
    {
        if (!sender->m_bOnMission || !CCoopMission::IsScriptAuthority(sender)) return;
        packet->owner = sender->m_playerId;
        if (packet->kind != P::SESSION)
        {
            auto actor = CNetworkPlayerManager::GetPlayer(packet->actor);
            if (!actor || actor == sender || actor->m_bIndividualMode ||
                !CCoopMission::CanReceiveMissionPacket(actor)) return;
        }
        for (auto player : CNetworkPlayerManager::m_pPlayers)
            if (player != sender && !player->m_bIndividualMode && CCoopMission::CanReceiveMissionPacket(player))
                GetPacketFactory().Send(*packet, player);
    }
    else if (packet->kind == P::INPUT || packet->kind == P::THROW || packet->kind == P::READY)
    {
        auto owner = CNetworkPlayerManager::GetPlayer(packet->owner);
        if (!owner || owner == sender || owner->m_bIndividualMode || !owner->m_bOnMission ||
            !CCoopMission::IsScriptAuthority(owner) || !CCoopMission::CanReceiveMissionPacket(owner)) return;
        packet->actor = sender->m_playerId;
        GetPacketFactory().Send(*packet, owner);
    }
}
