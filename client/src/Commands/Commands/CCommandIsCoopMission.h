#pragma once
#include "../CCustomCommand.h"
class CCommandIsCoopMission :
    public CCustomCommand
{
    // Inherited via CCustomCommand
    void Process(CRunningScript* script) override;
};
