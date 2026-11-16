#pragma once
#include "../CCustomCommand.h"
class CCommandGrantOddjobRewardToNetworkPlayers :
    public CCustomCommand
{
    // Inherited via CCustomCommand
    void Process(CRunningScript* script) override;
};
