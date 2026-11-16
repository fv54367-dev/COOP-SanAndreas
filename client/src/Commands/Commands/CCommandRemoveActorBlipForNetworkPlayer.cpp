#include "stdafx.h"
#include "CMirrorBlips.h"
#include "CCommandsForAllNetworkPlayers.h"
#include "CCommandRemoveActorBlipForNetworkPlayer.h"

void CCommandRemoveActorBlipForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(2);
	// the mission mirror layer sends the authority's blips as they are (CMirrorBlips): nothing to send here
	if (CMirrorBlips::MutesCoopBlipCommands())
		return;

	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (!networkPlayer)
		return;

	Packets::Blips::RemoveEntityBlip packet{};
	packet.forWhoPlayerId = networkPlayer->m_playerId;
	if (auto pEntity = CPools::GetPed(ScriptParams[1]))
		DropDeferredEntityBlip(networkPlayer->m_playerId, pEntity);
	packet.entity.SetEntity(CPools::GetPed(ScriptParams[1]));
	GetPacketFactory().Send(packet);
}
