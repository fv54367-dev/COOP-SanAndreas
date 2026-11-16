#include "stdafx.h"
#include "CCommandIsNetworkPlayerActorValid.h"
#include <CCoopMission.h>

void CCommandIsNetworkPlayerActorValid::Process(CRunningScript* script)
{
    script->CollectParameters(1);

    if (!ScriptParams[0])
    {
        script->UpdateCompareFlag(false);
        return;
    }

    if (auto ped = CPools::GetPed(ScriptParams[0]))
    {
        auto networkPlayer = CNetworkPlayerManager::GetPlayer(ped);
        script->UpdateCompareFlag(networkPlayer != nullptr && CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer));
    }
    else
    {
        script->UpdateCompareFlag(false);
    }
}

