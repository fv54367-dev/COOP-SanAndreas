#include "stdafx.h"
#include "CCommandMinigameVsReport.h"
#include "CMinigameVs.h"

void CCommandMinigameVsReport::Process(CRunningScript* script)
{
    script->CollectParameters(3);

    CMinigameVs::Report(ScriptParams[0], ScriptParams[1], ScriptParams[2]);
}
