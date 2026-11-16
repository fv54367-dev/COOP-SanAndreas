#include "stdafx.h"
#include "CCommandLaunchMissionForNetworkPlayers.h"
#include <CMissionMode.h>
#include <mission_ids.h>

void CCommandLaunchMissionForNetworkPlayers::Process(CRunningScript* script)
{
    script->CollectParameters(1);
    int missionId = ScriptParams[0];

    // alone (his choice or an individual-only mission): the others keep to their own game
    if (!CLocalPlayer::m_bIsHost || CMissionMode::IsIndividual() || IsIndividualOnlyMissionId(missionId))
        return;

    if (missionId < 0 || missionId > 200)
        return;

    Packets::Scripts::LaunchMissionLocal packet{};
    packet.missionId = missionId;
    GetPacketFactory().Send(packet);
}
