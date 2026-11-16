#include "stdafx.h"
#include "CMirrorBlips.h"
#include "CCommandClearAllEntityBlipsForNetworkPlayer.h"

void CCommandClearAllEntityBlipsForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(1);
	// the mission mirror layer sends the authority's blips as they are (CMirrorBlips): nothing to send here
	if (CMirrorBlips::MutesCoopBlipCommands())
		return;
	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (!networkPlayer)
		return;
	Packets::Blips::ClearEntityBlips packet{};
	packet.forWhoPlayerId = networkPlayer->m_playerId;
	GetPacketFactory().Send(packet);
}
