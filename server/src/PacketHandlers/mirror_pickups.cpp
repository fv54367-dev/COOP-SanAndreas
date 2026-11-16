#include "network/packets/mirror_pickups.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CCoopMission.h"

// the mission mirror layer's pickups (client/src/CMirrorPickups.h): new / gone pickups from the script authority to
// the other players of its mission; a player's collected copy to the script authority alone
PACKET_HANDLER(ePacketType::MISSION_MIRROR_PICKUP, Packets::Mirror::MirrorPickup* pMirrorPickup, CNetworkPlayer* pNetworkPlayer)
{
    pMirrorPickup->playerid = pNetworkPlayer->m_playerId;

    if (pMirrorPickup->op == MirrorPickupsWire::OP_COLLECTED)
    {
        if (!CCoopMission::CanReceiveMissionPacket(pNetworkPlayer))
            return;

        for (auto pAuthority : CNetworkPlayerManager::m_pPlayers)
        {
            if (pAuthority != pNetworkPlayer && CCoopMission::IsScriptAuthority(pAuthority))
            {
                GetPacketFactory().Send(*pMirrorPickup, pAuthority);
                return;
            }
        }
        return;
    }

    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
        CCoopMission::Relay(*pMirrorPickup, pNetworkPlayer, pNetworkPlayer);
}
