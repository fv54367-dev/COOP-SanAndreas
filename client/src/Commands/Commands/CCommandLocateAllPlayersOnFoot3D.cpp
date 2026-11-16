#include "stdafx.h"
#include "CCommandLocateAllPlayersOnFoot3D.h"
#include <CCoopMission.h>
#include <CMissionMode.h>
#include <CMissionLock.h>
#include <CMissionMirror.h>

// SWEET (the launcher at Sweet's house, one marker for all his missions) picks the mission by $flag_sweet_mission_counter, the
// global at byte offset 1808 of the compiled main.scm (SWEET_440: value 6 launches Cesar Vialpando, mission 19, individual
// only: docs/MISIONES_MODO.md). tools\check_main_layout.py keeps MAIN frozen, so the offset holds.
// the mission SWEET starts at his marker: 13 + the counter for the first seven (Tagging Up Turf .. Cesar Vialpando), -1 if
// it is another launcher or a later mission (then CMissionLock remembers it after the first blocked launch)
static int GetSweetCounter(CRunningScript* script)
{
	if (_strnicmp(script->m_szName, "SWEET", 5) != 0)
		return -1;

	const int counter = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1808]);
	return (counter >= 0 && counter <= 255) ? counter : -1;
}

static int GetSweetMissionId(CRunningScript* script)
{
	const int counter = GetSweetCounter(script);
	return (counter >= 0 && counter <= 6) ? 13 + counter : -1;
}

static bool IsIndividualOnlyStart(CRunningScript* script)
{
	return _strnicmp(script->m_szName, "SWEET", 5) == 0 && *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1808]) == 6;
}

void CCommandLocateAllPlayersOnFoot3D::Process(CRunningScript* script)
{
	script->CollectParameters(7);

	// every client tells the others which mission Sweet's marker would start for him next (progress is per player)
	const int sweetCounter = GetSweetCounter(script);
	{
		static DWORD lastEntryLog = 0;
		if (GetTickCount() - lastEntryLog > 3000 && (sweetCounter == 6 || _strnicmp(script->m_szName, "SW", 2) == 0))
		{
			lastEntryLog = GetTickCount();
			char line[160];
			sprintf_s(line, "sweet-entry name=%.8s counter=%d alone=%d auth=%d indiv=%d", script->m_szName, sweetCounter, (CMissionMode::IsPlayingAlone() || IsIndividualOnlyStart(script)) ? 1 : 0, CCoopMission::IsScriptAuthority() ? 1 : 0, CMissionMode::IsIndividual() ? 1 : 0);
			CMissionMirror::LogEvent(line);
		}
	}
	if (sweetCounter >= 0)
		CMissionMode::UpdateSweetProgress(sweetCounter);

	// alone: the player chose individual, or every other one did, or the mission is only played alone. Then the marker only
	// asks for him and any client can start it; in coop only the script authority (the host) runs it
	const bool bAlone = CMissionMode::IsPlayingAlone() || IsIndividualOnlyStart(script) || CCoopMission::IsLocalIndividualMission();

	if (!bAlone && !CCoopMission::IsScriptAuthority())
	{
		// a coop guest does not start anything here, but if he stands in the marker and the host's progress is not his, say it
		std::string name;
		int theirCounter = 0;
		if (sweetCounter >= 0 && CMissionMode::FindSweetMismatch(sweetCounter, name, theirCounter) &&
			Command<Commands::LOCATE_CHAR_ON_FOOT_3D>(CPools::GetPedRef(FindPlayerPed(0)), ((CVector*)&ScriptParams[1])->x,
				((CVector*)&ScriptParams[1])->y, ((CVector*)&ScriptParams[1])->z, ((CVector*)&ScriptParams[4])->x,
				((CVector*)&ScriptParams[4])->y, ((CVector*)&ScriptParams[4])->z, false))
		{
			CMissionMode::ShowProgressMismatch(name, theirCounter, sweetCounter);
		}

		script->UpdateCompareFlag(false);
		return;
	}

	bool showMarker = ScriptParams[0] != 0;
	CVector* position = (CVector*)&ScriptParams[1];
	CVector* radius = (CVector*)&ScriptParams[4];
	
	bool result = Command<Commands::LOCATE_CHAR_ON_FOOT_3D>(
		CPools::GetPedRef(FindPlayerPed(0)), 
		position->x, position->y, position->z, 
		radius->x, radius->y, radius->z, 
		showMarker);

	const bool bLocalIn = result;
	// the host standing in the marker is told as well when the progress differs, even if the others are not in it yet
	if (bLocalIn && !bAlone && sweetCounter >= 0)
	{
		std::string name;
		int theirCounter = 0;
		if (CMissionMode::FindSweetMismatch(sweetCounter, name, theirCounter))
		{
			CMissionMode::ShowProgressMismatch(name, theirCounter, sweetCounter);
			script->UpdateCompareFlag(false);
			return;
		}
	}
	if (result && !bAlone)
	{
		for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
		{
			// the ones that play alone are not waited for
			if (networkPlayer->m_pPed && !networkPlayer->m_bIndividualMode && CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
			{
				result = Command<Commands::LOCATE_CHAR_ON_FOOT_3D>(
					CPools::GetPedRef(networkPlayer->m_pPed), 
					position->x, position->y, position->z, 
					radius->x, radius->y, radius->z, 
					false);
			
				if (!result)
				{
					break;
				}
			}
		}
	}

	// a coop start needs everybody on the same mission: the progress of each one is his own
	if (result && !bAlone && sweetCounter >= 0)
	{
		std::string name;
		int theirCounter = 0;
		if (CMissionMode::FindSweetMismatch(sweetCounter, name, theirCounter))
		{
			CMissionMode::ShowProgressMismatch(name, theirCounter, sweetCounter);
			result = false;
		}
	}

	// somebody else plays the mission this marker would start (a story mission): it must not even start
	const bool bInMarker = bLocalIn;
	if (result && CMissionLock::IsLaunchBlocked(script, GetSweetMissionId(script)))
		result = false;

	// Cesar Vialpando is not played in coop: if he cannot start, tell him to switch to individual mode
	if (bInMarker && !result && IsIndividualOnlyStart(script) && !CMissionMode::IsIndividual())
		CMissionMode::ShowIndividualOnlyHint();

	// diagnostics: why does the marker start (or not) the mission
	static DWORD lastLog = 0;
	if (bInMarker && sweetCounter >= 0 && GetTickCount() - lastLog > 2000)
	{
		lastLog = GetTickCount();
		char line[160];
		sprintf_s(line, "sweet-marker counter=%d alone=%d result=%d name=%.8s auth=%d", sweetCounter, bAlone ? 1 : 0, result ? 1 : 0, script->m_szName, CCoopMission::IsScriptAuthority() ? 1 : 0);
		CMissionMirror::LogEvent(line);
	}

	script->UpdateCompareFlag(result);
}
