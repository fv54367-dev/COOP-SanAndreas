#pragma once

#include "network/packets/players.h"

// the server keeps each player's state between sessions (key: nickname);
// it is applied once after connecting and then sent back every few seconds
class CPersistenceSync
{
public:
    static void Reset();
    static void HandleState(Packets::Players::PlayerPersistState* pPacket);
    static void Process(unsigned int tickCount);
    // the server asked for our state now (forced save, "/guardar"): send it on the next Process, not in 10 s
    static void RequestImmediateSend();

private:
    static bool CanApplyOrCollect();
    static void Apply(const Packets::Players::SPersistData& data);
    static bool Collect(Packets::Players::SPersistData& data);
};
