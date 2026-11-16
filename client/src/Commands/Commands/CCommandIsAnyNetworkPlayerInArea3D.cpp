#include "stdafx.h"
#include "CCommandIsAnyNetworkPlayerInArea3D.h"

// Coop.IsAnyNetworkPlayerInArea3D (0x1D2B) x y z rx ry rz: true when another player (alive, in our interior) stands in
// the box. Shops with a single seat (BARB: the barber chair) use it so only one player at a time sits on it.
void CCommandIsAnyNetworkPlayerInArea3D::Process(CRunningScript* script)
{
	script->CollectParameters(6);

	const CVector position = *(CVector*)&ScriptParams[0];
	const CVector radius = *(CVector*)&ScriptParams[3];
	script->UpdateCompareFlag(IsAnyNetworkPlayerInArea(position, radius));
}

bool CCommandIsAnyNetworkPlayerInArea3D::IsAnyNetworkPlayerInArea(const CVector& position, const CVector& radius)
{
	CPlayerPed* localPlayer = FindPlayerPed(0);

	for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
	{
		CPlayerPed* ped = networkPlayer ? networkPlayer->m_pPed : nullptr;
		if (!ped || ped == localPlayer || !ped->m_matrix || ped->m_fHealth <= 0.0f)
			continue;

		// a remote copy whose area is still unknown counts (interiors sit far from the map anyway)
		if (localPlayer && ped->m_nAreaCode != 0 && ped->m_nAreaCode != localPlayer->m_nAreaCode)
			continue;

		const CVector& pos = ped->GetPosition();
		if (fabs(pos.x - position.x) <= radius.x && fabs(pos.y - position.y) <= radius.y && fabs(pos.z - position.z) <= radius.z)
		{
			return true;
		}
	}

	return false;
}
