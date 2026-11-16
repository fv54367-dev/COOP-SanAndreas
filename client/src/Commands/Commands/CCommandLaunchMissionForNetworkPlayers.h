#pragma once
#include "../CCustomCommand.h"

class CCommandLaunchMissionForNetworkPlayers : public CCustomCommand
{
    // Inherited via CCustomCommand
    void Process(CRunningScript* script) override;
};
