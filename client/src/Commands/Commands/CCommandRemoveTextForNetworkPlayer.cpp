#include "stdafx.h"
#include "COpCodeSync.h"
#include "CCommandRemoveTextForNetworkPlayer.h"

void CCommandRemoveTextForNetworkPlayer::Process(CRunningScript* script)
{
	char gxt[9];
	script->ReadTextLabelFromScript(gxt, 8);
	gxt[8] = '\0';
	COpCodeSync::MarkPerPlayerText(script, gxt);

	script->CollectParameters(1);

	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (!networkPlayer)
		return;
	
	Packets::Scripts::RemoveMessageGXT packet{};
	strncpy(packet.gxt, gxt, 8);
	packet.forWhoPlayerId = networkPlayer->m_playerId;
	GetPacketFactory().Send(packet);
}
