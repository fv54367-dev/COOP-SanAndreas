#include "stdafx.h"
#include "CCommandRemoveCheckpointForNetworkPlayer.h"

void CCommandRemoveCheckpointForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(1);

	Packets::Blips::RemoveCheckpoint packet{};
	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (!networkPlayer)
		return;
	packet.forWhoPlayerId = networkPlayer->m_playerId;
	GetPacketFactory().Send(packet);
}
