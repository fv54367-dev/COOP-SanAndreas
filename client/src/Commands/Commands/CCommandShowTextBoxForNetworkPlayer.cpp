#include "stdafx.h"
#include "COpCodeSync.h"
#include "CCommandShowTextBoxForNetworkPlayer.h"

void CCommandShowTextBoxForNetworkPlayer::Process(CRunningScript* script)
{
	char gxt[8];
	script->ReadTextLabelFromScript(gxt, 8);
	gxt[7] = '\0';
	COpCodeSync::MarkPerPlayerText(script, gxt);

	script->CollectParameters(1);

	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (!networkPlayer)
		return;

	Packets::Scripts::AddMessageGXT packet{};
	strncpy(packet.gxt, gxt, 8);
	packet.forWhoPlayerId = networkPlayer->m_playerId;
	packet.type = Packets::Scripts::AddMessageGXT::eGXTMsgType::sync_COMMAND_PRINT_HELP;
	GetPacketFactory().Send(packet);
}
