#include "stdafx.h"
#include "CCommandGrantOddjobRewardToNetworkPlayers.h"
#include <CCoopMission.h>

void CCommandGrantOddjobRewardToNetworkPlayers::Process(CRunningScript* script)
{
	script->CollectParameters(1);
	uint8_t rewardId = (uint8_t)ScriptParams[0];
	tScriptParam* pFlag = script->GetPointerToScriptVariable(1);

	int32_t offset = -1;
	if (pFlag)
	{
		offset = (int32_t)((uint8_t*)pFlag - (uint8_t*)CTheScripts::ScriptSpace);
		if (offset < 0 || offset >= 200000)
			offset = -1;
	}

	script->CollectParameters(3);

	if (!CCoopMission::IsScriptAuthority())
		return;

	for (int i = 0; i < 3; i++)
	{
		if (ScriptParams[i] == -1)
			continue;

		if (auto ped = CPools::GetPed(ScriptParams[i]))
		{
			if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(ped))
			{
				if (!CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
					continue;

				Packets::Scripts::OddjobReward packet{};
				packet.playerid = networkPlayer->m_playerId;
				packet.rewardId = rewardId;
				packet.doneFlagOffset = offset;
				GetPacketFactory().Send(packet);
			}
		}
	}
}
