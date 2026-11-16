#include "stdafx.h"
#include "CMirrorBlips.h"
#include "CCommandUpdateActorBlipForNetworkPlayer.h"
#include "CCommandsForAllNetworkPlayers.h"

void CCommandUpdateActorBlipForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(6);
	// the mission mirror layer sends the authority's blips as they are (CMirrorBlips): nothing to send here
	if (CMirrorBlips::MutesCoopBlipCommands())
		return;

	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	CPed* pPed = CPools::GetPed(ScriptParams[1]);
	if (!networkPlayer || !pPed)
		return;

	// a char created this frame has no network id yet: the blip waits for it (it was lost before, SWEET4's Ballas)
	SendOrDeferEntityBlip(networkPlayer->m_playerId, pPed, ScriptParams[2], ScriptParams[3], ScriptParams[4], ScriptParams[5]);
}
