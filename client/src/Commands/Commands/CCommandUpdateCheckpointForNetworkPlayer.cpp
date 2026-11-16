#include "stdafx.h"
#include "CCommandUpdateCheckpointForNetworkPlayer.h"

void CCommandUpdateCheckpointForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(7);
	
	//CChat::AddMessage("CCommandUpdateCheckpointForNetworkPlayer::Process");
	Packets::Blips::UpdateCheckpoint packet{};
	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[6]));
	if (!networkPlayer)
		return;
	packet.forWhoPlayerId = networkPlayer->m_playerId;
	packet.position = *(CVector*)&ScriptParams[0];
	packet.radius = *(CVector*)&ScriptParams[3];
	GetPacketFactory().Send(packet);
}
