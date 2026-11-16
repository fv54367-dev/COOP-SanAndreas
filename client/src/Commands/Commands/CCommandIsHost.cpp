#include "stdafx.h"
#include "CCommandIsHost.h"

// INTROST (after Big Smoke) is each player's own: the voice-over at the hub, the save help, Ryder's contact blip and
// $funeral_mission_finished = 3, which MOB_LA1 waits for to make Ryder's call (19). Only on the host, the other players
// never got that call nor Ryder's marker; changing the script would move the offsets of MAIN
static bool IsPerPlayerScript(CRunningScript* script)
{
	return script && _strnicmp(script->m_szName, "introst", 8) == 0;
}

void CCommandIsHost::Process(CRunningScript* script)
{
	script->UpdateCompareFlag(CLocalPlayer::m_bIsHost || IsPerPlayerScript(script));
}
