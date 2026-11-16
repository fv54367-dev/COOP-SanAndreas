#include "stdafx.h"
#include "CCommandIsCoopMission.h"
#include "CCoopMission.h"
#include "COpCodeSync.h"

// Coop.IsCoopMission (0x1D2A): true only for the script that runs the active coop mission. It is the script
// ChooseSideMissionMode renamed and started syncing, so a coop mission in progress plus a synced script is enough.
void CCommandIsCoopMission::Process(CRunningScript* script)
{
	script->UpdateCompareFlag(CCoopMission::IsRunning() && COpCodeSync::IsScriptSynced(script));
}
