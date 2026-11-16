#pragma once
#include "../CCustomCommand.h"

class CCommandChooseSideMissionMode : public CCustomCommand
{
    void Process(CRunningScript* script) override;
};
