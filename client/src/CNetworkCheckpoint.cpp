#include "stdafx.h"
#include "CNetworkCheckpoint.h"
#include <CNetworkStaticBlip.h>
#include <CMissionMirror.h>
#include <CMissionAreaMarkers.h>

void CNetworkCheckpoint::Process()
{
	if (ms_bEnabled)
	{
		// diagnostics: where the guest is in relation to the checkpoint he should see
		static DWORD s_nLastLog = 0;
		const DWORD now = GetTickCount();
		if (now - s_nLastLog > 3000)
		{
			s_nLastLog = now;
			CPlayerPed* pPlayer = FindPlayerPed(0);
			CMissionMirror::LogEventf("checkpoint-active pos=%.1f,%.1f,%.1f radius=%.1f dist=%.1f blip=%d", ms_vecPosition.x,
				ms_vecPosition.y, ms_vecPosition.z, ms_vecRadius.x,
				pPlayer ? (pPlayer->GetPosition() - ms_vecPosition).Magnitude() : -1.0f, ms_nBlipHandle);
		}
		float fToY = ms_vecRadius.y + ms_vecPosition.y;
		float fToX = ms_vecRadius.x + ms_vecPosition.x;
		float fFromY = ms_vecPosition.y - ms_vecRadius.y;
		float fFromX = ms_vecPosition.x - ms_vecRadius.x;
		// the host's own cylinder already comes through CMissionAreaMarkers: not drawn twice
		if (!CMissionAreaMarkers::IsDrawnNear(ms_vecPosition))
			CTheScripts::HighlightImportantArea(*(int*)&ms_vecRadius.x, fFromX, fFromY, fToX, fToY, ms_vecPosition.z); // TODO(v0.3.1-alpha): is this height calculation correct?
	}
}

void CNetworkCheckpoint::Update(CVector& vecPosition, CVector& vecRadius)
{
	//CChat::AddMessage("CNetworkCheckpoint::Update(CVector(%f, %f, %f), CVector(%f, %f, %f))",
		//vecPosition.x, vecPosition.y, vecPosition.z,
		//vecRadius.x, vecRadius.y, vecRadius.z);

	const bool bSameBlipPosition = ms_nBlipHandle >= 0 && ms_vecBlipPosition.x == vecPosition.x &&
		ms_vecBlipPosition.y == vecPosition.y && ms_vecBlipPosition.z == vecPosition.z;

	ms_vecPosition = vecPosition;
	ms_vecRadius = vecRadius;
	ms_bEnabled = true;

	// only the clients that did not run the script get here (see PacketHandlers/blips.cpp), so the handle is local
	if (bSameBlipPosition)
		return;

	ClearBlip();

	ms_nBlipHandle = CRadar::SetCoordBlip(BLIP_COORD, vecPosition, BLIP_COLOUR_RED, BLIP_DISPLAY_BLIP_ONLY, nullptr);
	// the script's add_blip_for_coord (0x47CFE0) makes the blip size 3, not the default 1: a guest saw it tiny
	if (ms_nBlipHandle >= 0)
		CRadar::ChangeBlipScale(ms_nBlipHandle, 3);
	ms_vecBlipPosition = vecPosition;

	// the checkpoint belongs to the mission, not to the world: keep it out of the static blip snapshot
	CNetworkStaticBlip::MarkMissionBlip(CRadar::GetActualBlipArrayIndex(ms_nBlipHandle), true);
}

void CNetworkCheckpoint::Remove()
{
	//CChat::AddMessage("CNetworkCheckpoint::Remove()");
	ms_bEnabled = false;
	ClearBlip();
}

void CNetworkCheckpoint::ClearBlip()
{
	if (ms_nBlipHandle < 0)
		return;

	// the slot can be reused by another blip: it is not a mission one anymore
	CNetworkStaticBlip::MarkMissionBlip(CRadar::GetActualBlipArrayIndex(ms_nBlipHandle), false);
	CRadar::ClearBlip(ms_nBlipHandle);
	ms_nBlipHandle = -1;
}
