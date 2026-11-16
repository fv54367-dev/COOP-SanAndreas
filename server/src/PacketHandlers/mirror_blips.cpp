// Ported from project_gta_sa_coop (sametozbalkan, GPL-3.0), package M2, and adapted.
#include "network/packets/mirror_blips.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CCoopMission.h"

// the mission mirror layer (client/src/CMirrorBlips.h): the script authority's mission blips, from the authority only,
// with the sender filled in; a snapshot goes to the player it names, everything else to the other players of the
// mission (CCoopMission::Relay, as MISSION_AREA_MARKER)
PACKET_HANDLER(ePacketType::MISSION_MIRROR_BLIPS, Packets::Mirror::MirrorBlips* pMirrorBlips, CNetworkPlayer* pNetworkPlayer)
{
    if (!CCoopMission::IsScriptAuthority(pNetworkPlayer))
        return;

    pMirrorBlips->playerid = pNetworkPlayer->m_playerId;
    if (pMirrorBlips->targetPlayerId.valid())
    {
        auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pMirrorBlips->targetPlayerId);
        if (pTargetPlayer && pTargetPlayer != pNetworkPlayer && CCoopMission::CanReceiveMissionPacket(pTargetPlayer))
            GetPacketFactory().Send(*pMirrorBlips, pTargetPlayer);
        return;
    }
    CCoopMission::Relay(*pMirrorBlips, pNetworkPlayer, pNetworkPlayer);
}
