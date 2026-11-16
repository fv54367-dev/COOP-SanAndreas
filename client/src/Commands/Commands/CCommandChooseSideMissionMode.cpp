#include "stdafx.h"
#include "CCommandChooseSideMissionMode.h"
#include "CCoopMission.h"

// Coop.ChooseSideMissionMode (0x1D29): false while the local player decides (Y / N) whether the side mission is
// cooperative, true once it is decided. On the coop path CCoopMission renames the running script and starts syncing it.
void CCommandChooseSideMissionMode::Process(CRunningScript* script)
{
    char szScriptName[8]{};
    char szTitle[24]{};

    script->ReadTextLabelFromScript(szScriptName, sizeof(szScriptName));
    script->ReadTextLabelFromScript(szTitle, sizeof(szTitle));

    szScriptName[7] = '\0';
    szTitle[23] = '\0';

    script->UpdateCompareFlag(CCoopMission::ChooseSideMissionMode(script, szScriptName, szTitle));
}
