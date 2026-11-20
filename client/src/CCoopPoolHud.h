#pragma once

#include "network/packets/scripts.h"

class CRunningScript;

namespace CoopPoolHud {
    using Hud = Packets::Scripts::CoopPool::Hud;
    void BeforeCommand(const CRunningScript& script, uint16_t opcode);
    void AfterCommand(uint16_t opcode);
    Hud Snapshot();
    uint32_t Hash(const Hud& hud);
    void Draw(const Hud& hud);
    void Reset(bool worldGone);
}
