#pragma once
#include <cstdint>
class CRunningScript;
namespace Packets::Scripts { class CoopPool; }
class CCoopPool {
public:
    static bool Enabled();
    static bool OwnView();
    static bool IsPool(CRunningScript* script);
    static bool FilterCompare(CRunningScript* script, uint16_t opcode, bool state);
    static void BeforeCommand(CRunningScript* script, uint16_t opcode);
    static void AfterCommand(CRunningScript* script, uint16_t opcode);
    static void Process();
    static void Reset();
    static void OnPacket(const Packets::Scripts::CoopPool& packet);
};
