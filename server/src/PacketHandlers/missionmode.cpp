#include "network/packet_types.h"
#include "network/packets/scripts.h"
#include "stdafx.h"
#include "CCoopMission.h"

PACKET_HANDLER(ePacketType::PLAYER_MISSION_MODE, Packets::Scripts::PlayerMissionMode* pMode, CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr)
        return;

    // back to coop in the middle of the authority's mission: that mission is not his any more, he gets the next one
    if (pNetworkPlayer->m_bIndividualMode && !pMode->individual && CCoopMission::IsAuthorityOnMission() &&
        !CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        pNetworkPlayer->m_bLateForMission = true;
    }
    pNetworkPlayer->m_bIndividualMode = pMode->individual;
    pNetworkPlayer->m_bVanillaScripts = pMode->vanillaScripts;
    pNetworkPlayer->m_nRunningMission = pMode->runningMission;

    // everybody learns how this player plays the missions (the client sends it again when somebody joins)
    pMode->playerId = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pMode, pNetworkPlayer);
}

// the progress of a coop story mission that was passed: only the script authority tells the others
PACKET_HANDLER(ePacketType::STORY_PROGRESS_SYNC, Packets::Scripts::StoryProgressSync* pProgress, CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr || !CCoopMission::IsScriptAuthority(pNetworkPlayer))
        return;

    GetPacketFactory().SendToAll(*pProgress, pNetworkPlayer);
}
