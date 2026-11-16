#include "network/packet_types.h"
#include "network/packets/scripts.h"
#include "stdafx.h"
#include "CStoryProgress.h"
#include "CMirrorBlips.h"
#include "CCoopMission.h"
#include "COpCodeSync.h"
#include "CConfigLoader.h"

PACKET_HANDLER(ePacketType::PLAYER_MISSION_MODE, Packets::Scripts::PlayerMissionMode* pMode)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pMode->playerId);
    if (pNetworkPlayer == nullptr)
        return;

    pNetworkPlayer->m_nSweetProgress = pMode->sweetProgress;
    pNetworkPlayer->m_nRunningMission = pMode->runningMission;
    memcpy(pNetworkPlayer->m_anChainProgress, pMode->chainProgress, sizeof(pNetworkPlayer->m_anChainProgress));
    pNetworkPlayer->m_nStoryLaunchers = pMode->storyLaunchers;

    // the original scripts and ours run other missions: neither waits for the other nor shares one with him
    const bool bOtherScripts = pMode->vanillaScripts != CConfigLoader::ms_bVanillaScripts;
    if (bOtherScripts != pNetworkPlayer->m_bOtherScripts)
    {
        pNetworkPlayer->m_bOtherScripts = bOtherScripts;
        if (bOtherScripts)
        {
            CChat::AddMessage("{ff6666}[Coop]{d6dce8} " + pNetworkPlayer->GetName() + " usa " +
                              (pMode->vanillaScripts ? "los scripts ORIGINALES" : "los scripts COOP") +
                              " y vos no: no pueden jugar misiones juntos. Usen el mismo MODO_SCRIPTS_*.bat.");
        }
    }

    if (pNetworkPlayer->m_bIndividualMode == pMode->individual)
        return;

    pNetworkPlayer->m_bIndividualMode = pMode->individual;

    // back to coop in the middle of the authority's mission: the server relays none of it to him (as one that connected
    // during it), so it doesn't wait for him nor fails with him until the next one
    if (!pMode->individual && CCoopMission::IsScriptAuthority() && CTheScripts::OnAMissionFlag &&
        CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] && COpCodeSync::IsSyncedMissionRunning())
    {
        pNetworkPlayer->m_bLateForMission = true;
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} " + pNetworkPlayer->GetName() + " entra en la proxima mision.");
    }

    // back to coop: the script authority sends him its mission's blips again (he dropped them, CMirrorBlips)
    if (!pMode->individual)
        CMirrorBlips::OnPlayerJoined(pNetworkPlayer->m_playerId);

    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} " + pNetworkPlayer->GetName() +
                      (pMode->individual ? " juega las misiones en modo individual." : " juega las misiones en modo coop."));
}

PACKET_HANDLER(ePacketType::STORY_PROGRESS_SYNC, Packets::Scripts::StoryProgressSync* pProgress)
{
    CStoryProgress::Apply(*pProgress);
}
