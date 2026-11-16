#pragma once
#include "../CCustomCommand.h"
class CCommandIsAnyNetworkPlayerInArea3D :
    public CCustomCommand
{
public:
    static bool IsAnyNetworkPlayerInArea(const CVector& position, const CVector& radius);

private:
    // Inherited via CCustomCommand
    void Process(CRunningScript* script) override;
};
