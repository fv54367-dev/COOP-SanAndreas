#include "stdafx.h"
#include "CMirrorBlips.h"
#include "CCommandUpdateCarBlipForNetworkPlayer.h"
#include "CCommandsForAllNetworkPlayers.h"

void CCommandUpdateCarBlipForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(6);
	// the mission mirror layer sends the authority's blips as they are (CMirrorBlips): nothing to send here
	if (CMirrorBlips::MutesCoopBlipCommands())
		return;

	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	CVehicle* pVehicle = CPools::GetVehicle(ScriptParams[1]);
	if (!networkPlayer || !pVehicle)
		return;

	// a car created this frame has no network id yet: the blip waits for it
	SendOrDeferEntityBlip(networkPlayer->m_playerId, pVehicle, ScriptParams[2], ScriptParams[3], ScriptParams[4], ScriptParams[5]);
}
