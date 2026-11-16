#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/gangs.h"

// Territories (CZoneInfo) and gang wars are simulated by the host only, these packets are just relayed
PACKET_HANDLER(ePacketType::GANG_ZONES_SYNC, Packets::Gangs::GangZonesSync* pGangZonesSync, CNetworkPlayer* pNetworkPlayer)
{
    if (!pNetworkPlayer->m_bIsHost)
    {
        return;
    }

    GetPacketFactory().SendToAll(*pGangZonesSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::GANG_WAR_STATE, Packets::Gangs::GangWarState* pGangWarState, CNetworkPlayer* pNetworkPlayer)
{
    if (!pNetworkPlayer->m_bIsHost)
    {
        return;
    }

    GetPacketFactory().SendToAll(*pGangWarState, pNetworkPlayer);
}
