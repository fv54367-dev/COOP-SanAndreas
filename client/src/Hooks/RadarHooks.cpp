#include "stdafx.h"
#include "RadarHooks.h"
#include "CNetworkStaticBlip.h"
#include <COpCodeSync.h>
#include <CCoopMission.h>
#include <CEntryExit.h>
#include "Commands/CommandHooks.h"

// The blip hooks only receive the blip, not the script that created it. `GetCurrentScript` is the script running the
// opcode being processed, so it is the one that created the blip (see `CommandHooks`).
static bool IsLocalMissionScriptRunning(bool* pSyncedMission = nullptr)
{
	if (pSyncedMission)
		*pSyncedMission = false;

	// the opcodes a client receives from the host run in the fake `coopand` script: their blips are not ours
	if (COpCodeSync::bProcessingNetworkOpcode)
		return false;

	CRunningScript* current = CommandHooks::GetCurrentScript();

	if (!current)
		return false;

	// the pointer is kept after the script ends, so only trust it while it is still in the active list
	for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
	{
		if (script == current)
		{
			// a mission synced to the other players (story mission) shows its blips to all of them: they travel in the snapshot
			if (COpCodeSync::IsScriptSynced(current) && CCoopMission::IsScriptAuthority())
			{
				if (pSyncedMission)
					*pSyncedMission = current->m_bIsMission;
				return false;
			}

			return current->m_bIsMission;
		}
	}

	return false;
}

/// <param name="created">the hook created the blip (as opposed to changing or clearing it)</param>
void CheckAndMarkRadarSync(int blipHandle, bool created = false)
{
	const auto index = CRadar::GetActualBlipArrayIndex(blipHandle);

	if (index == -1)
		return;

	// the blip belongs to the local player's own mission: it is kept out of the world snapshot (see CNetworkStaticBlip).
	// A contact point is a mission launcher (the next story mission's icon), world state every player must see even when a
	// mission script leaves it: INTRO creates Big Smoke's icon on the host only
	if (created && CConfigLoader::ms_bCoopSideMissions)
	{
		bool syncedMission = false;
		const bool missionBlip = IsLocalMissionScriptRunning(&syncedMission);
		const bool contactPoint = CRadar::ms_RadarTrace[index].m_nBlipType == BLIP_CONTACTPOINT;
		CNetworkStaticBlip::MarkMissionBlip(index, !contactPoint && missionBlip);
		CNetworkStaticBlip::MarkSyncedMissionBlip(index, !contactPoint && syncedMission);
	}

	if (CLocalPlayer::m_bIsHost &&
		CNetworkStaticBlip::IsAllowedSyncingRadarSprite(static_cast<eRadarSprite>(CRadar::ms_RadarTrace[index].m_nRadarSprite)))
	{
		CNetworkStaticBlip::ms_bNeedToSendAfterThisFrame = true;
	}
}

void CRadar__SetBlipSprite_Hook(int blipHandle, char spriteId)
{
	CRadar::SetBlipSprite(blipHandle, spriteId);
	CheckAndMarkRadarSync(blipHandle);
}

int CRadar__SetCoordBlip_Hook(eBlipType type, CVector posn, int a5, eBlipDisplay display)
{
	int result = CRadar::SetCoordBlip(type, posn, a5, display, nullptr);
	CheckAndMarkRadarSync(result, true);
	return result;
}

void CRadar__ClearBlip_Hook(int blipHandle)
{
	// the slot can be reused by another blip (a world one): it is no longer a mission blip
	CheckAndMarkRadarSync(blipHandle);
	CNetworkStaticBlip::MarkMissionBlip(CRadar::GetActualBlipArrayIndex(blipHandle), false);

	CRadar::ClearBlip(blipHandle);
}

void CRadar__ChangeBlipColour_Hook(int blipHandle, int color)
{
	CRadar::ChangeBlipColour(blipHandle, color);
	CheckAndMarkRadarSync(blipHandle);
}

void CRadar__SetCoordBlipAppearance_Hook(int blipHandle, uint8_t appearance)
{
	CRadar::SetCoordBlipAppearance(blipHandle, appearance);
	CheckAndMarkRadarSync(blipHandle);
}

void CRadar__SetBlipFriendly_Hook(int blipHandle, bool friendly)
{
	CRadar::SetBlipFriendly(blipHandle, friendly);
	CheckAndMarkRadarSync(blipHandle);
}

void CRadar__SetBlipEntryExit_Hook(int blipHandle, CEntryExit* enex)
{
	CRadar::SetBlipEntryExit(blipHandle, enex);
	CheckAndMarkRadarSync(blipHandle);
}

void RadarHooks::InjectHooks()
{
	patch::RedirectCall(0x444403, CRadar__SetBlipSprite_Hook);
	patch::RedirectCall(0x47F7FE, CRadar__SetBlipSprite_Hook);
	patch::RedirectCall(0x48BCA8, CRadar__SetBlipSprite_Hook);
	patch::RedirectCall(0x48DBE1, CRadar__SetBlipSprite_Hook);

	patch::RedirectCall(0x47C744, CRadar__SetCoordBlip_Hook);
	patch::RedirectCall(0x47CFE0, CRadar__SetCoordBlip_Hook);
	patch::RedirectCall(0x47F74C, CRadar__SetCoordBlip_Hook);
	patch::RedirectCall(0x47F7EF, CRadar__SetCoordBlip_Hook); // ADD_SPRITE_BLIP_FOR_COORD (02A8)
	patch::RedirectCall(0x58394E, CRadar__SetCoordBlip_Hook);

	patch::RedirectCall(0x446EC0, CRadar__ClearBlip_Hook);
	patch::RedirectCall(0x47C670, CRadar__ClearBlip_Hook);

	patch::RedirectCall(0x47C698, CRadar__ChangeBlipColour_Hook);

	patch::RedirectCall(0x476114, CRadar__SetCoordBlipAppearance_Hook);

	patch::RedirectCall(0x47285D, CRadar__SetBlipFriendly_Hook);

	patch::RedirectCall(0x475882, CRadar__SetBlipEntryExit_Hook);
}
