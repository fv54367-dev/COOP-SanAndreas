#include "stdafx.h"
#include "CCoopMission.h"
#include "network/packet_handler.h"
#include "network/packets/story_help.h"

// story-help-optional: the offer goes from the script authority to a player, his answer back to the authority, and the
// end of the mission from the authority to the players it offered. The server only checks who sends it and relays it.
PACKET_HANDLER(ePacketType::STORY_HELP, Packets::Scripts::StoryHelp* packet, CNetworkPlayer* sender)
{
    using P = Packets::Scripts::StoryHelp;
    if (packet->kind >= P::KIND_COUNT)
        return;
    auto target = CNetworkPlayerManager::GetPlayer(packet->target);
    if (!target || target == sender)
        return;
    const bool fromAuthority = packet->kind == P::OFFER || packet->kind == P::END;
    if (fromAuthority ? !CCoopMission::IsScriptAuthority(sender) : !CCoopMission::IsScriptAuthority(target))
        return;
    packet->senderPlayerid = sender->m_playerId;
    packet->mission[sizeof(packet->mission) - 1] = '\0';
    GetPacketFactory().Send(*packet, target);
}

// the contact points the authority's passed story mission removed and made: every other player gets it and applies it only
// if he had the mission pending (CStoryHelp)
PACKET_HANDLER(ePacketType::STORY_CONTACTS, Packets::Scripts::StoryContacts* packet, CNetworkPlayer* sender)
{
    if (!CCoopMission::IsScriptAuthority(sender))
        return;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
        if (player != sender && !player->m_bIndividualMode)
            GetPacketFactory().Send(*packet, player);
}
