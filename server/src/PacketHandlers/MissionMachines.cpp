#include "stdafx.h"
#include "CCoopMission.h"
#include "network/packet_handler.h"
#include "network/packets/mission_machines.h"

// the machine of the running mission, offered to the guests: only the script authority may tell it, and only the
// guests of that mission hear it (T67/T70)
PACKET_HANDLER(ePacketType::MACHINE_PHASE, Packets::Scripts::MachinePhase* packet, CNetworkPlayer* sender)
{
    if (!packet->kind || packet->kind > Packets::Scripts::MachinePhase::FORKLIFT || sender->m_bIndividualMode ||
        !sender->m_bOnMission || !CCoopMission::CanReceiveMissionPacket(sender) || !CCoopMission::IsScriptAuthority(sender))
        return;
    packet->senderPlayerid = sender->m_playerId;
    packet->owner = sender->m_playerId;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
        if (player != sender && !player->m_bIndividualMode && CCoopMission::CanReceiveMissionPacket(player))
            GetPacketFactory().Send(*packet, player);
}

// what a guest's own machine did to a shared mission vehicle: only the script authority applies it (he owns the
// real vehicle; T67)
PACKET_HANDLER(ePacketType::GUEST_MACHINE_DAMAGE, Packets::Scripts::GuestMachineDamage* packet, CNetworkPlayer* sender)
{
    if (!packet->vehicleid.valid() || packet->health < 0.0f || sender->m_bIndividualMode ||
        !CCoopMission::CanReceiveMissionPacket(sender))
        return;
    packet->senderPlayerid = sender->m_playerId;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        if (player == sender || player->m_bIndividualMode || !CCoopMission::IsScriptAuthority(player) ||
            !CCoopMission::CanReceiveMissionPacket(player))
            continue;
        GetPacketFactory().Send(*packet, player);
        break;  // one authority: the mission's owner, else the host
    }
}

// where a guest's copy of a mission object now stands (his machine moved it): only the script authority applies
// it to the real object (T70)
PACKET_HANDLER(ePacketType::GUEST_OBJECT_MOVE, Packets::Scripts::GuestObjectMove* packet, CNetworkPlayer* sender)
{
    if (!packet->objectid || sender->m_bIndividualMode || !CCoopMission::CanReceiveMissionPacket(sender))
        return;
    packet->senderPlayerid = sender->m_playerId;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        if (player == sender || player->m_bIndividualMode || !CCoopMission::IsScriptAuthority(player) ||
            !CCoopMission::CanReceiveMissionPacket(player))
            continue;
        GetPacketFactory().Send(*packet, player);
        break;  // one authority: the mission's owner, else the host
    }
}
