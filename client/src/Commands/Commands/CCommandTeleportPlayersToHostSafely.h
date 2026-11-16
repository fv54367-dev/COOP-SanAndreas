#pragma once
#include "../CCustomCommand.h"
class CCommandTeleportPlayersToHostSafely :
    public CCustomCommand
{
    // Inherited via CCustomCommand
    void Process(CRunningScript* script) override;

public:
    static void TeleportToHost(CNetworkPlayer** networkPlayers, int playerCount);
    // the same safe spots around a target for up to 3 players (slot i = out[i]); a slot left at 0,0,0 found no place
    static void FindSafeSpots(const CVector& target, float heading, int count, CVector* out);
    static void ProcessPendingTeleport();
    static void ClearPendingTeleport();
};

class CCommandTeleportAllNetworkPlayersToHost : public CCustomCommand
{
    void Process(CRunningScript* script) override;
};

